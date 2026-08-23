// Composite pass: port of the horizon-fade part of Onyx's quad.frag. The
// resolved highway texture is drawn over the background colour inside the
// track rectangle; rows above `startFade` (measured from the bottom, 0..1)
// fade linearly to nothing at `endFade`. FXAA and the venue post-processing in
// quad.frag are not ported.

cbuffer FadeCB : register(b0) {
    float2 gRectMin;   // track rect in NDC (x: -1..1, y: -1..1), bottom-left
    float2 gRectMax;   // top-right
    float gStartFade;  // Onyx view.track-fade.bottom
    float gEndFade;    // Onyx view.track-fade.top
    float2 _pad;
};

Texture2D gScene : register(t0);
SamplerState gSamp : register(s0);

struct PSIn {
    float4 clip : SV_Position;
    float2 uv : TEXCOORD0;
};

// Two triangles covering the rect, wound counter-clockwise (the rasterizer
// culls clockwise faces, as Onyx's GL state does); no vertex buffer.
PSIn VSMain(uint id : SV_VertexID) {
    float2 corners[6] = {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(1, 0), float2(1, 1), float2(0, 1)
    };
    float2 c = corners[id];
    PSIn o;
    float2 ndc = lerp(gRectMin, gRectMax, c);
    o.clip = float4(ndc, 0.0, 1.0);
    // D3D texture v grows downward: c.y = 1 is the rect top, which samples v = 0.
    o.uv = float2(c.x, 1.0 - c.y);
    return o;
}

float4 PSMain(PSIn i) : SV_Target {
    float4 result = gScene.Sample(gSamp, i.uv);
    float yFromBottom = 1.0 - i.uv.y;  // Onyx TexCoord.y: 0 = bottom
    if (gEndFade > gStartFade) {
        float horizonFade = 1.0 - (yFromBottom - gStartFade) / (gEndFade - gStartFade);
        if (horizonFade > 1.0) horizonFade = 1.0;
        if (horizonFade < 0.0) horizonFade = 0.0;
        result = float4(result.rgb, result.a * horizonFade);
    }
    return result;
}
