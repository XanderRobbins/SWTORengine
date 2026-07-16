// FXAA (Fast Approximate Anti-Aliasing), compact FXAA 3.11 variant after
// Timothy Lottes' whitepaper (public domain algorithm). Runs at capture
// resolution BEFORE EASU — FSR expects anti-aliased input.

Texture2D InputTexture : register(t0);
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer FxaaConstants : register(b0) {
    float2 TexSize;      // full input texture size
    float2 ViewportSize; // valid content region (<= TexSize)
    float TextProtect;   // 0..1: mask glyph-like pixels back to the original
    float3 _pad;
};

#define FXAA_REDUCE_MIN (1.0 / 128.0)
#define FXAA_REDUCE_MUL (1.0 / 8.0)
#define FXAA_SPAN_MAX 8.0

static const float3 kLuma = float3(0.299, 0.587, 0.114);

float3 SampleClamped(float2 uv, float2 uvMax) {
    return InputTexture.SampleLevel(LinearSampler, min(uv, uvMax), 0).rgb;
}

[numthreads(8, 8, 1)]
void mainCS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)ViewportSize.x || id.y >= (uint)ViewportSize.y) return;

    const float2 rcpTex = 1.0 / TexSize;
    const float2 uv = (float2(id.xy) + 0.5) * rcpTex;
    // Never sample beyond the valid content region (texture may be larger).
    const float2 uvMax = (ViewportSize - 0.5) * rcpTex;

    float3 rgbNW = SampleClamped(uv + float2(-1.0, -1.0) * rcpTex, uvMax);
    float3 rgbNE = SampleClamped(uv + float2(1.0, -1.0) * rcpTex, uvMax);
    float3 rgbSW = SampleClamped(uv + float2(-1.0, 1.0) * rcpTex, uvMax);
    float3 rgbSE = SampleClamped(uv + float2(1.0, 1.0) * rcpTex, uvMax);
    float3 rgbM = SampleClamped(uv, uvMax);

    float lumaNW = dot(rgbNW, kLuma);
    float lumaNE = dot(rgbNE, kLuma);
    float lumaSW = dot(rgbSW, kLuma);
    float lumaSE = dot(rgbSE, kLuma);
    float lumaM = dot(rgbM, kLuma);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce =
        max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, -FXAA_SPAN_MAX, FXAA_SPAN_MAX) * rcpTex;

    float3 rgbA = 0.5 * (SampleClamped(uv + dir * (1.0 / 3.0 - 0.5), uvMax) +
                         SampleClamped(uv + dir * (2.0 / 3.0 - 0.5), uvMax));
    float3 rgbB = rgbA * 0.5 + 0.25 * (SampleClamped(uv + dir * -0.5, uvMax) +
                                       SampleClamped(uv + dir * 0.5, uvMax));

    float lumaB = dot(rgbB, kLuma);
    float3 result = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;

    // --- Text protection ---
    // UI glyphs are thin strokes: both neighbors at distance 1-2 match each
    // other while the center differs sharply. Long geometry edges (what FXAA
    // is for) fail this test because their two sides differ. Mask detected
    // glyph pixels back toward the unfiltered original.
    if (TextProtect > 0.0) {
        float lumaL = dot(SampleClamped(uv + float2(-1.0, 0.0) * rcpTex, uvMax), kLuma);
        float lumaR = dot(SampleClamped(uv + float2(1.0, 0.0) * rcpTex, uvMax), kLuma);
        float lumaU = dot(SampleClamped(uv + float2(0.0, -1.0) * rcpTex, uvMax), kLuma);
        float lumaD = dot(SampleClamped(uv + float2(0.0, 1.0) * rcpTex, uvMax), kLuma);
        float lumaL2 = dot(SampleClamped(uv + float2(-2.0, 0.0) * rcpTex, uvMax), kLuma);
        float lumaR2 = dot(SampleClamped(uv + float2(2.0, 0.0) * rcpTex, uvMax), kLuma);
        float lumaU2 = dot(SampleClamped(uv + float2(0.0, -2.0) * rcpTex, uvMax), kLuma);
        float lumaD2 = dot(SampleClamped(uv + float2(0.0, 2.0) * rcpTex, uvMax), kLuma);

        // stroke width 1 (centered) and width 2 (center on either column)
        float v1 = min(abs(lumaM - lumaL), abs(lumaM - lumaR)) - 0.5 * abs(lumaL - lumaR);
        float v2 = min(abs(lumaM - lumaL), abs(lumaM - lumaR2)) - 0.5 * abs(lumaL - lumaR2);
        float v3 = min(abs(lumaM - lumaL2), abs(lumaM - lumaR)) - 0.5 * abs(lumaL2 - lumaR);
        float h1 = min(abs(lumaM - lumaU), abs(lumaM - lumaD)) - 0.5 * abs(lumaU - lumaD);
        float h2 = min(abs(lumaM - lumaU), abs(lumaM - lumaD2)) - 0.5 * abs(lumaU - lumaD2);
        float h3 = min(abs(lumaM - lumaU2), abs(lumaM - lumaD)) - 0.5 * abs(lumaU2 - lumaD);

        float stroke = max(max(v1, max(v2, v3)), max(h1, max(h2, h3)));
        float textness = saturate(stroke * 6.0 * TextProtect);
        result = lerp(result, rgbM, textness);
    }

    OutputTexture[id.xy] = float4(result, 1.0);
}
