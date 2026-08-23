#include "ui/icons.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "app/config.h"
#include "image/decode.h"

namespace hydra::ui {

namespace {

// Loads one PNG from disk into a DX11 texture + shader-resource-view, in the
// same DXGI_FORMAT_R8G8B8A8_UNORM layout imgui_impl_dx11 uses for the font
// atlas. Returns 0 (and leaves the icon as a plain marker fallback) on any
// read/decode/GPU failure rather than asserting -- resource/ is best-effort,
// matching main.cpp's app-icon load right above where load_icons is called.
ImTextureID load_png_texture(ID3D11Device* device, const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return 0;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(f);
        return 0;
    }
    std::vector<uint8_t> buf((size_t)size);
    size_t read = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    if (read != buf.size()) return 0;

    hydra::image::DecodedImage img = hydra::image::decode_image(buf);
    if (img.empty()) return 0;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = (UINT)img.width;
    desc.Height = (UINT)img.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub = {};
    sub.pSysMem = img.rgba.data();
    sub.SysMemPitch = (UINT)img.width * 4;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, &sub, &texture);
    if (FAILED(hr) || !texture) return 0;

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    hr = device->CreateShaderResourceView(texture, &srv_desc, &srv);
    texture->Release();
    if (FAILED(hr) || !srv) return 0;

    return (ImTextureID)(intptr_t)srv;
}

}  // namespace

void load_icons(ID3D11Device* device) {
    // exe-relative, not cwd-relative: the app may be launched with any
    // working directory (e.g. a shortcut's Start-in), like db_path/ini_path.
    const std::string dir = hydra::app::exe_dir() + "\\resource\\";
    g_icon_record = load_png_texture(device, (dir + "icon_record_32.png").c_str());
    g_icon_star = load_png_texture(device, (dir + "icon_star_32.png").c_str());
    g_icon_pencil = load_png_texture(device, (dir + "icon_pencil_32.png").c_str());
    g_icon_hash = load_png_texture(device, (dir + "icon_hash_32.png").c_str());
}

}  // namespace hydra::ui
