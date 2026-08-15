// Song-info icon textures (record/star/pencil/hash), matching hydra_app.py's
// dpg.add_static_texture loads of resource/icon_{record,star,pencil,hash}_32.png.
// main.cpp decodes and uploads these once the DX11 device exists; views pull
// the resulting SRVs from here, mirroring fonts.h's g_mono_font pattern.

#ifndef HYDRA_UI_ICONS_H
#define HYDRA_UI_ICONS_H

#include "imgui.h"
#include <d3d11.h>

namespace hydra::ui {

inline ImTextureID g_icon_record = 0;
inline ImTextureID g_icon_star = 0;
inline ImTextureID g_icon_pencil = 0;
inline ImTextureID g_icon_hash = 0;

// Decodes resource/icon_*_32.png (best-effort: a missing/unreadable file
// leaves that icon's texture ID at 0, and callers fall back to a plain
// marker) and uploads each as a DX11 texture via `device`. Call once after
// the D3D11 device is created and before the first frame.
void load_icons(ID3D11Device* device);

}  // namespace hydra::ui

#endif  // HYDRA_UI_ICONS_H
