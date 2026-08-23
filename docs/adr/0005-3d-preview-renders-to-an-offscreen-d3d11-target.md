# The 3D Preview renders to an offscreen D3D11 target composited into ImGui

Hydra is a single ImGui window backed by one Direct3D 11 device and swap chain
(`src/ui/main.cpp`), and the Preview lives inside the Song Details modal. Rather
than render the 3D highway to the main back buffer behind or around the UI, the
Preview draws its scene to an **offscreen render target** (a texture with its own
render-target and depth views) and shows the result inside the modal with
`ImGui::Image` on the texture's shader-resource view. This keeps the 3D content
inside the existing ImGui layout — clipped to its panel, sized to the tab, drawn
in the normal widget order — instead of fighting the UI for the back buffer. The
scene is drawn from the shared D3D11 device each frame the Preview tab is open,
and not at all when it is closed. The alternative (compositing 3D directly into
the swap chain) was rejected because it entangles the highway's viewport, depth,
and clear state with ImGui's own back-buffer pass. Keep the Preview's device
objects (RT texture, views, depth, pipeline state) owned by the preview
controller and rebuilt on resize; do not draw the highway to the main back
buffer.
