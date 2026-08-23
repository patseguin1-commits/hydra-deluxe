// Headless Direct3D helpers for the renderer tests: a WARP (software) device,
// so no GPU or display is needed, and a pixel read-back from a texture's SRV.

#ifndef HYDRA_TESTS_WARP_UTIL_H
#define HYDRA_TESTS_WARP_UTIL_H

#include <cstdint>
#include <cstring>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

namespace warp {

inline bool make_device(Microsoft::WRL::ComPtr<ID3D11Device>& device,
                        Microsoft::WRL::ComPtr<ID3D11DeviceContext>& context) {
    D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &want, 1,
                                   D3D11_SDK_VERSION, &device, &got, &context);
    return SUCCEEDED(hr);
}

// Copy a colour texture back to CPU memory as tightly-packed RGBA, top row first.
inline std::vector<uint8_t> read_pixels(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                        ID3D11ShaderResourceView* srv, int w, int h) {
    Microsoft::WRL::ComPtr<ID3D11Resource> res;
    srv->GetResource(&res);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    res.As(&tex);
    D3D11_TEXTURE2D_DESC d;
    tex->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    dev->CreateTexture2D(&d, nullptr, &staging);
    ctx->CopyResource(staging.Get(), tex.Get());

    D3D11_MAPPED_SUBRESOURCE m;
    ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m);
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
    const uint8_t* src = static_cast<const uint8_t*>(m.pData);
    for (int y = 0; y < h; ++y)
        std::memcpy(&out[static_cast<size_t>(y) * w * 4], src + static_cast<size_t>(y) * m.RowPitch,
                    static_cast<size_t>(w) * 4);
    ctx->Unmap(staging.Get(), 0);
    return out;
}

inline const uint8_t* pixel(const std::vector<uint8_t>& img, int w, int x, int y) {
    return &img[(static_cast<size_t>(y) * w + x) * 4];
}

}  // namespace warp

#endif  // HYDRA_TESTS_WARP_UTIL_H
