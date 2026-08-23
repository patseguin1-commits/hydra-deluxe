// Port of Onyx's object.vert + object.frag (onyx-resources/shaders), line for
// line. Phong point light, no attenuation, no gamma; a material is a flat colour
// or one texture optionally overlaid by a second (ghost/accent). Onyx quirks are
// kept on purpose: output alpha = result.a * alpha (so lit texels exceed 1 and
// the ONE/ONE alpha blend saturates), and colorOverlay premultiplies c1 without
// un-premultiplying. Matrices are row-major (DirectXMath row vectors).

cbuffer PerFrame : register(b0) {
    row_major float4x4 gView;
    row_major float4x4 gProj;
    float3 gViewPos;
    float _pad0;
};

cbuffer PerObject : register(b1) {
    row_major float4x4 gModel;
    row_major float4x4 gNormalMat;  // transpose(inverse(model)), computed on the CPU
    float3 gLightPos;
    float _pad1;
    float4 gLightAmbient;
    float4 gLightDiffuse;
    float4 gLightSpecular;
    uint gDiffuseType;   // 1 = colour, 2 = texture, 3 = texture + overlay
    float3 _pad2;
    float4 gDiffuseColor;
    float4 gSpecularColor;  // Onyx hard-codes (0.5, 0.5, 0.5, 1) (type 1)
    float gShininess;       // Onyx: 32
    float gAlpha;
    float2 _pad3;
};

Texture2D gImage : register(t0);
Texture2D gImage2 : register(t1);
SamplerState gSamp : register(s0);

struct VSIn {
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct PSIn {
    float4 clip : SV_Position;
    float3 fragPos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

PSIn VSMain(VSIn i) {
    PSIn o;
    float4 wp = mul(float4(i.pos, 1.0), gModel);
    o.clip = mul(mul(wp, gView), gProj);
    o.fragPos = wp.xyz;
    o.normal = mul(float4(i.normal, 0.0), gNormalMat).xyz;
    o.uv = i.uv;
    return o;
}

float4 colorOverlay(float4 c1, float4 c2) {
    return float4(c1.rgb * c1.a * (1.0 - c2.a) + c2.rgb * c2.a,
                  c1.a * (1.0 - c2.a) + c2.a);
}

float4 getDiffuse(float2 uv) {
    if (gDiffuseType == 1) {
        return gDiffuseColor;
    } else if (gDiffuseType == 2) {
        return gImage.Sample(gSamp, uv);
    } else if (gDiffuseType == 3) {
        float4 color1 = gImage.Sample(gSamp, uv);
        float4 color2 = gImage2.Sample(gSamp, uv);
        return colorOverlay(color1, color2);
    }
    return float4(1.0, 0.0, 1.0, 1.0);  // magenta on invalid input
}

float4 PSMain(PSIn i) : SV_Target {
    float4 diffuseSource = getDiffuse(i.uv);
    float4 specularSource = gSpecularColor;

    // ambient
    float4 ambient = gLightAmbient * diffuseSource;

    // diffuse
    float3 norm = normalize(i.normal);
    float3 lightDir = normalize(gLightPos - i.fragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    float4 diffuse = gLightDiffuse * diff * diffuseSource;

    // specular
    float3 viewDir = normalize(gViewPos - i.fragPos);
    float3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), gShininess);
    float4 specular = gLightSpecular * (spec * specularSource);

    float4 result = ambient + diffuse + specular;
    return float4(result.rgb, result.a * gAlpha);
}
