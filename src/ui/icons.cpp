#include "ui/icons.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#include <cstdio>
#include <vector>

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
    std::vector<unsigned char> buf((size_t)size);
    size_t read = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    if (read != buf.size()) return 0;

    int width = 0, height = 0, channels = 0;
    unsigned char* pixels =
        stbi_load_from_memory(buf.data(), (int)buf.size(), &width, &height, &channels, 4);
    if (!pixels) return 0;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub = {};
    sub.pSysMem = pixels;
    sub.SysMemPitch = (UINT)width * 4;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, &sub, &texture);
    stbi_image_free(pixels);
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
    g_icon_record = load_png_texture(device, "resource/icon_record_32.png");
    g_icon_star = load_png_texture(device, "resource/icon_star_32.png");
    g_icon_pencil = load_png_texture(device, "resource/icon_pencil_32.png");
    g_icon_hash = load_png_texture(device, "resource/icon_hash_32.png");
}

}  // namespace hydra::ui
