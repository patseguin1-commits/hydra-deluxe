#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "render/preview_renderer.h"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "image/decode.h"
#include "render/file_util.h"
#include "render/highway_draw.h"
#include "render/obj_loader.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using hydra::app::PreviewScene;

namespace hydra::render {

namespace {

void check(HRESULT hr, const char* what) {
    if (FAILED(hr)) throw std::runtime_error(std::string("PreviewRenderer: ") + what);
}

// Constant buffers, laid out exactly as assets/preview/shaders declare them.
struct PerFrame {
    XMFLOAT4X4 view;
    XMFLOAT4X4 proj;
    XMFLOAT3 view_pos;
    float pad0;
};
static_assert(sizeof(PerFrame) % 16 == 0, "PerFrame must be 16-byte aligned");

struct PerObject {
    XMFLOAT4X4 model;
    XMFLOAT4X4 normal;
    XMFLOAT3 light_pos;
    float pad1;
    XMFLOAT4 light_ambient;
    XMFLOAT4 light_diffuse;
    XMFLOAT4 light_specular;
    uint32_t diffuse_type;
    XMFLOAT3 pad2;
    XMFLOAT4 diffuse_color;
    XMFLOAT4 specular_color;
    float shininess;
    float alpha;
    XMFLOAT2 pad3;
};
static_assert(sizeof(PerObject) % 16 == 0, "PerObject must be 16-byte aligned");

struct FadeCB {
    XMFLOAT2 rect_min;
    XMFLOAT2 rect_max;
    float start_fade;
    float end_fade;
    XMFLOAT2 pad;
};
static_assert(sizeof(FadeCB) % 16 == 0, "FadeCB must be 16-byte aligned");

XMFLOAT4 f4(const Color& c) { return XMFLOAT4(c.r, c.g, c.b, c.a); }

ComPtr<ID3DBlob> compile(const std::string& source, const char* name, const char* entry,
                         const char* target) {
    ComPtr<ID3DBlob> code, errors;
    HRESULT hr = D3DCompile(source.data(), source.size(), name, nullptr, nullptr, entry,
                            target, 0, 0, &code, &errors);
    if (FAILED(hr)) {
        std::string msg = std::string("PreviewRenderer: shader ") + name + " (" + entry + ")";
        if (errors) msg += ": " + std::string(static_cast<const char*>(errors->GetBufferPointer()),
                                              errors->GetBufferSize());
        throw std::runtime_error(msg);
    }
    return code;
}

struct GpuMesh {
    ComPtr<ID3D11Buffer> vb;
    UINT vertex_count = 0;
};

}  // namespace

struct PreviewRenderer::Impl {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    std::string asset_dir;
    PreviewConfig cfg;

    int width = 0, height = 0, track_h = 0;
    UINT msaa = 1;

    // Final target (what the GUI shows) and the scene target it composites.
    ComPtr<ID3D11Texture2D> final_tex, scene_tex, scene_resolved, depth_tex;
    ComPtr<ID3D11RenderTargetView> final_rtv, scene_rtv;
    ComPtr<ID3D11ShaderResourceView> final_srv, scene_srv;
    ComPtr<ID3D11DepthStencilView> dsv;

    ComPtr<ID3D11VertexShader> obj_vs, fade_vs;
    ComPtr<ID3D11PixelShader> obj_ps, fade_ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> cb_frame, cb_object, cb_fade;

    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> ds_less, ds_always, ds_off;
    ComPtr<ID3D11BlendState> blend_scene, blend_fade;
    ComPtr<ID3D11SamplerState> sampler;

    std::array<GpuMesh, 5> meshes;  // indexed by MeshId
    std::array<ComPtr<ID3D11ShaderResourceView>, static_cast<size_t>(TextureId::Count)> textures;

    TrackState state;
    bool have_state = false;

    GpuMesh upload(const ObjMesh& m) {
        GpuMesh g;
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = static_cast<UINT>(m.vertices.size() * sizeof(ObjVertex));
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = m.vertices.data();
        check(device->CreateBuffer(&bd, &sd, &g.vb), "vertex buffer");
        g.vertex_count = static_cast<UINT>(m.vertices.size());
        return g;
    }

    // Decode an image file and upload it with its rows flipped so that v = 0
    // samples the image's bottom row, as Onyx's GL upload does.
    ComPtr<ID3D11ShaderResourceView> load_texture(const std::string& file) {
        std::vector<uint8_t> bytes = read_file_bytes(asset_dir + "\\textures\\" + file);
        if (bytes.empty()) throw std::runtime_error("PreviewRenderer: missing texture " + file);
        image::DecodedImage img = image::decode_image(bytes);
        if (img.empty()) throw std::runtime_error("PreviewRenderer: undecodable texture " + file);
        std::vector<uint8_t> flipped(img.rgba.size());
        const size_t row = static_cast<size_t>(img.width) * 4;
        for (int y = 0; y < img.height; ++y)
            std::memcpy(&flipped[static_cast<size_t>(y) * row],
                        &img.rgba[static_cast<size_t>(img.height - 1 - y) * row], row);
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = static_cast<UINT>(img.width);
        td.Height = static_cast<UINT>(img.height);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = flipped.data();
        sd.SysMemPitch = static_cast<UINT>(row);
        ComPtr<ID3D11Texture2D> tex;
        check(device->CreateTexture2D(&td, &sd, &tex), "texture");
        ComPtr<ID3D11ShaderResourceView> srv;
        check(device->CreateShaderResourceView(tex.Get(), nullptr, &srv), "texture srv");
        return srv;
    }

    GpuMesh load_model(const char* file) {
        std::string text = read_file_text(asset_dir + "\\models\\" + file);
        if (text.empty()) throw std::runtime_error(std::string("PreviewRenderer: missing model ") + file);
        return upload(load_obj(text));
    }

    void create_targets() {
        final_tex.Reset(); scene_tex.Reset(); scene_resolved.Reset(); depth_tex.Reset();
        final_rtv.Reset(); scene_rtv.Reset(); final_srv.Reset(); scene_srv.Reset(); dsv.Reset();

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = static_cast<UINT>(width);
        td.Height = static_cast<UINT>(height);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        check(device->CreateTexture2D(&td, nullptr, &final_tex), "final texture");
        check(device->CreateRenderTargetView(final_tex.Get(), nullptr, &final_rtv), "final rtv");
        check(device->CreateShaderResourceView(final_tex.Get(), nullptr, &final_srv), "final srv");

        // The scene target is the track rectangle, multisampled when supported.
        D3D11_TEXTURE2D_DESC sd = td;
        sd.Height = static_cast<UINT>(track_h);
        UINT quality = 0;
        msaa = 1;
        const int want = std::max(1, cfg.hydra.msaa);
        if (want > 1 &&
            SUCCEEDED(device->CheckMultisampleQualityLevels(sd.Format, static_cast<UINT>(want), &quality)) &&
            quality > 0)
            msaa = static_cast<UINT>(want);
        sd.SampleDesc.Count = msaa;
        sd.BindFlags = D3D11_BIND_RENDER_TARGET;
        check(device->CreateTexture2D(&sd, nullptr, &scene_tex), "scene texture");
        check(device->CreateRenderTargetView(scene_tex.Get(), nullptr, &scene_rtv), "scene rtv");

        D3D11_TEXTURE2D_DESC rd = td;
        rd.Height = static_cast<UINT>(track_h);
        rd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        check(device->CreateTexture2D(&rd, nullptr, &scene_resolved), "resolve texture");
        check(device->CreateShaderResourceView(scene_resolved.Get(), nullptr, &scene_srv), "scene srv");

        D3D11_TEXTURE2D_DESC dd = sd;
        dd.Format = DXGI_FORMAT_D32_FLOAT;
        dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        check(device->CreateTexture2D(&dd, nullptr, &depth_tex), "depth texture");
        check(device->CreateDepthStencilView(depth_tex.Get(), nullptr, &dsv), "dsv");
    }

    template <class T>
    void update_cb(ID3D11Buffer* cb, const T& data) {
        D3D11_MAPPED_SUBRESOURCE m;
        check(context->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "map cbuffer");
        std::memcpy(m.pData, &data, sizeof(T));
        context->Unmap(cb, 0);
    }

    void draw_command(const DrawCommand& cmd) {
        PerObject po = {};
        XMMATRIX model = stretch_matrix(cmd);
        XMStoreFloat4x4(&po.model, model);
        XMStoreFloat4x4(&po.normal, XMMatrixTranspose(XMMatrixInverse(nullptr, model)));
        LightConfig light = light_for(cfg, cmd);
        po.light_pos = XMFLOAT3(light.position.x, light.position.y, light.position.z);
        po.light_ambient = f4(light.ambient);
        po.light_diffuse = f4(light.diffuse);
        po.light_specular = f4(light.specular);
        po.diffuse_type = cmd.material.kind == MaterialKind::Color ? 1u
                          : cmd.material.kind == MaterialKind::Texture ? 2u : 3u;
        po.diffuse_color = f4(cmd.material.color);
        po.specular_color = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);  // Onyx's hard-coded specular
        po.shininess = 32.0f;
        po.alpha = cmd.alpha;
        update_cb(cb_object.Get(), po);

        ID3D11ShaderResourceView* srvs[2] = {nullptr, nullptr};
        if (cmd.material.kind != MaterialKind::Color)
            srvs[0] = textures[static_cast<size_t>(cmd.material.texture)].Get();
        if (cmd.material.kind == MaterialKind::TextureOverlay)
            srvs[1] = textures[static_cast<size_t>(cmd.material.overlay)].Get();
        context->PSSetShaderResources(0, 2, srvs);

        context->OMSetDepthStencilState(
            cmd.depth == DepthMode::Always ? ds_always.Get() : ds_less.Get(), 0);

        const GpuMesh& mesh = meshes[static_cast<size_t>(cmd.mesh)];
        ID3D11Buffer* vb = mesh.vb.Get();
        UINT stride = sizeof(ObjVertex), offset = 0;
        context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        context->Draw(mesh.vertex_count, 0);
    }
};

PreviewRenderer::PreviewRenderer(ID3D11Device* device, ID3D11DeviceContext* context,
                                 const std::string& asset_dir)
    : impl_(new Impl) {
    Impl& d = *impl_;
    d.device = device;
    d.context = context;
    d.asset_dir = asset_dir;

    std::string cfg_text = read_file_text(asset_dir + "\\3d-config.json");
    if (cfg_text.empty())
        throw std::runtime_error("PreviewRenderer: missing " + asset_dir + "\\3d-config.json");
    d.cfg = load_preview_config(cfg_text);

    // Shaders from files, like Onyx loads its GLSL.
    std::string obj_src = read_file_text(asset_dir + "\\shaders\\object.hlsl");
    std::string fade_src = read_file_text(asset_dir + "\\shaders\\fade.hlsl");
    if (obj_src.empty() || fade_src.empty())
        throw std::runtime_error("PreviewRenderer: missing shader files in " + asset_dir);
    ComPtr<ID3DBlob> ovs = compile(obj_src, "object.hlsl", "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> ops = compile(obj_src, "object.hlsl", "PSMain", "ps_5_0");
    ComPtr<ID3DBlob> fvs = compile(fade_src, "fade.hlsl", "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> fps = compile(fade_src, "fade.hlsl", "PSMain", "ps_5_0");
    check(device->CreateVertexShader(ovs->GetBufferPointer(), ovs->GetBufferSize(), nullptr, &d.obj_vs), "object vs");
    check(device->CreatePixelShader(ops->GetBufferPointer(), ops->GetBufferSize(), nullptr, &d.obj_ps), "object ps");
    check(device->CreateVertexShader(fvs->GetBufferPointer(), fvs->GetBufferSize(), nullptr, &d.fade_vs), "fade vs");
    check(device->CreatePixelShader(fps->GetBufferPointer(), fps->GetBufferSize(), nullptr, &d.fade_ps), "fade ps");

    const D3D11_INPUT_ELEMENT_DESC elems[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    check(device->CreateInputLayout(elems, _countof(elems), ovs->GetBufferPointer(),
                                    ovs->GetBufferSize(), &d.layout), "input layout");

    auto make_cb = [&](UINT size, ComPtr<ID3D11Buffer>& out, const char* what) {
        D3D11_BUFFER_DESC cbd = {};
        cbd.Usage = D3D11_USAGE_DYNAMIC;
        cbd.ByteWidth = size;
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        check(device->CreateBuffer(&cbd, nullptr, &out), what);
    };
    make_cb(sizeof(PerFrame), d.cb_frame, "per-frame cbuffer");
    make_cb(sizeof(PerObject), d.cb_object, "per-object cbuffer");
    make_cb(sizeof(FadeCB), d.cb_fade, "fade cbuffer");

    // Onyx GL state: cull back faces with counter-clockwise fronts.
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_BACK;
    rd.FrontCounterClockwise = TRUE;
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = TRUE;
    check(device->CreateRasterizerState(&rd, &d.raster), "rasterizer");

    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS;
    check(device->CreateDepthStencilState(&dd, &d.ds_less), "ds less");
    dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    check(device->CreateDepthStencilState(&dd, &d.ds_always), "ds always");
    dd.DepthEnable = FALSE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    check(device->CreateDepthStencilState(&dd, &d.ds_off), "ds off");

    // Onyx blend: colour SrcAlpha/InvSrcAlpha, alpha One/One.
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    check(device->CreateBlendState(&bd, &d.blend_scene), "blend scene");
    // The composite keeps the final target opaque (ImGui blends by alpha).
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    check(device->CreateBlendState(&bd, &d.blend_fade), "blend fade");

    D3D11_SAMPLER_DESC smp = {};
    smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    smp.ComparisonFunc = D3D11_COMPARISON_NEVER;
    smp.MaxLOD = 0.0f;
    check(device->CreateSamplerState(&smp, &d.sampler), "sampler");

    // The verbatim Onyx drum models and the two built-in shapes.
    d.meshes[static_cast<size_t>(MeshId::Tom)] = d.load_model("drum-tom.obj");
    d.meshes[static_cast<size_t>(MeshId::Cymbal)] = d.load_model("drum-cymbal.obj");
    d.meshes[static_cast<size_t>(MeshId::Kick)] = d.load_model("drum-kick.obj");
    d.meshes[static_cast<size_t>(MeshId::Flat)] = d.upload(make_flat_quad());
    d.meshes[static_cast<size_t>(MeshId::Box)] = d.upload(make_box());

    for (int i = 1; i < static_cast<int>(TextureId::Count); ++i)
        d.textures[static_cast<size_t>(i)] = d.load_texture(texture_file(static_cast<TextureId>(i)));
}

PreviewRenderer::~PreviewRenderer() { delete impl_; }

void PreviewRenderer::resize(int width, int height) {
    Impl& d = *impl_;
    d.width = std::max(1, width);
    d.height = std::max(1, height);
    d.track_h = std::min(d.height, static_cast<int>(std::lround(d.width * d.cfg.view.height_width_ratio)));
    d.track_h = std::max(1, d.track_h);
    d.create_targets();
}

void PreviewRenderer::set_scene(const PreviewScene& scene, const TrackStateOptions& opts) {
    impl_->state = build_track_state(scene, opts);
    impl_->have_state = true;
}

void PreviewRenderer::render(double now_ms, const RenderParams& params) {
    Impl& d = *impl_;
    if (!d.final_rtv) return;
    ID3D11DeviceContext* ctx = d.context;
    const PreviewConfig& cfg = d.cfg;

    // ---- scene pass: the highway into the (multisampled) track target ----
    std::vector<DrawCommand> cmds =
        build_highway_draws(d.state, cfg, now_ms / 1000.0, params.speed);

    HighwayCamera cam = make_camera(cfg, static_cast<float>(d.width) / static_cast<float>(d.track_h));
    PerFrame pf = {};
    pf.view = cam.view;
    pf.proj = cam.proj;
    pf.view_pos = XMFLOAT3(cam.view_pos.x, cam.view_pos.y, cam.view_pos.z);
    d.update_cb(d.cb_frame.Get(), pf);

    ID3D11RenderTargetView* rtv = d.scene_rtv.Get();
    ctx->OMSetRenderTargets(1, &rtv, d.dsv.Get());
    D3D11_VIEWPORT vp = {0, 0, static_cast<float>(d.width), static_cast<float>(d.track_h), 0.0f, 1.0f};
    ctx->RSSetViewports(1, &vp);
    const float clear[4] = {0, 0, 0, 0};
    ctx->ClearRenderTargetView(rtv, clear);
    ctx->ClearDepthStencilView(d.dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

    ctx->IASetInputLayout(d.layout.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(d.obj_vs.Get(), nullptr, 0);
    ctx->PSSetShader(d.obj_ps.Get(), nullptr, 0);
    ID3D11Buffer* cbs[2] = {d.cb_frame.Get(), d.cb_object.Get()};
    ctx->VSSetConstantBuffers(0, 2, cbs);
    ctx->PSSetConstantBuffers(0, 2, cbs);
    ID3D11SamplerState* samp = d.sampler.Get();
    ctx->PSSetSamplers(0, 1, &samp);
    ctx->RSSetState(d.raster.Get());
    const float blendf[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(d.blend_scene.Get(), blendf, 0xffffffff);

    for (const DrawCommand& cmd : cmds) d.draw_command(cmd);

    ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
    ctx->PSSetShaderResources(0, 2, none);
    ID3D11RenderTargetView* null_rtv = nullptr;
    ctx->OMSetRenderTargets(1, &null_rtv, nullptr);

    if (d.msaa > 1)
        ctx->ResolveSubresource(d.scene_resolved.Get(), 0, d.scene_tex.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    else
        ctx->CopyResource(d.scene_resolved.Get(), d.scene_tex.Get());

    // ---- composite pass: background colour + horizon-faded highway --------
    rtv = d.final_rtv.Get();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT fvp = {0, 0, static_cast<float>(d.width), static_cast<float>(d.height), 0.0f, 1.0f};
    ctx->RSSetViewports(1, &fvp);
    const float bg[4] = {cfg.view.background.r, cfg.view.background.g, cfg.view.background.b, 1.0f};
    ctx->ClearRenderTargetView(rtv, bg);

    FadeCB fc = {};
    // The track rect hugs the bottom: NDC y from -1 up to -1 + 2*track_h/height.
    fc.rect_min = XMFLOAT2(-1.0f, -1.0f);
    fc.rect_max = XMFLOAT2(1.0f, -1.0f + 2.0f * static_cast<float>(d.track_h) / static_cast<float>(d.height));
    fc.start_fade = cfg.view.track_fade_bottom;
    fc.end_fade = cfg.view.track_fade_top;
    d.update_cb(d.cb_fade.Get(), fc);

    ctx->IASetInputLayout(nullptr);
    ID3D11Buffer* no_vb = nullptr;
    UINT zero = 0;
    ctx->IASetVertexBuffers(0, 1, &no_vb, &zero, &zero);
    ctx->VSSetShader(d.fade_vs.Get(), nullptr, 0);
    ctx->PSSetShader(d.fade_ps.Get(), nullptr, 0);
    ID3D11Buffer* fcb = d.cb_fade.Get();
    ctx->VSSetConstantBuffers(0, 1, &fcb);
    ctx->PSSetConstantBuffers(0, 1, &fcb);
    ID3D11ShaderResourceView* scene_srv = d.scene_srv.Get();
    ctx->PSSetShaderResources(0, 1, &scene_srv);
    ctx->OMSetDepthStencilState(d.ds_off.Get(), 0);
    ctx->OMSetBlendState(d.blend_fade.Get(), blendf, 0xffffffff);
    ctx->Draw(6, 0);

    ctx->PSSetShaderResources(0, 2, none);
    ctx->OMSetRenderTargets(1, &null_rtv, nullptr);
    ctx->OMSetBlendState(nullptr, blendf, 0xffffffff);
    ctx->OMSetDepthStencilState(nullptr, 0);
}

ID3D11ShaderResourceView* PreviewRenderer::texture_srv() const { return impl_->final_srv.Get(); }
int PreviewRenderer::width() const { return impl_->width; }
int PreviewRenderer::height() const { return impl_->height; }
const PreviewConfig& PreviewRenderer::config() const { return impl_->cfg; }

}  // namespace hydra::render
