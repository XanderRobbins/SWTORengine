// Final post pass at output resolution, after RCAS:
//   deband -> exposure -> contrast -> gamma -> saturation/vibrance ->
//   filmic S-curve -> vignette -> film grain
// Runs only when any effect is non-neutral (pipeline skips it otherwise).
// Grain/vignette are last so the sharpener never amplifies them.

Texture2D InputTexture : register(t0);
Texture2D BlurTexture : register(t1);  // wide Gaussian of the input (for clarity)
Texture2D BloomTexture : register(t2); // half-res blurred bright-pass
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer PostConstants : register(b0) {
    float Vibrance;       // -1..1, extra saturation weighted to muted pixels
    float Saturation;     // 1 = neutral
    float Contrast;       // 1 = neutral
    float Gamma;          // 1 = neutral, >1 brightens
    float Exposure;       // stops, 0 = neutral
    float Filmic;         // 0..1 blend into smoothstep S-curve
    float Vignette;       // 0..1
    float Grain;          // 0..1
    float DebandStrength; // 0 = off, ~1 typical
    float FrameIndex;     // animates grain/deband jitter
    float2 OutSize;
    float Clarity;        // 0..1 local contrast (mid-frequency "depth")
    float Bloom;          // 0..1 additive glow strength
    float2 _pad;
};

static const float3 kLuma = float3(0.299, 0.587, 0.114);

float Hash(float2 p) {
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

float3 LoadClamped(int2 p) {
    p = clamp(p, int2(0, 0), int2(OutSize) - 1);
    return InputTexture.Load(int3(p, 0)).rgb;
}

[numthreads(8, 8, 1)]
void mainCS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)OutSize.x || id.y >= (uint)OutSize.y) return;

    const int2 pos = int2(id.xy);
    float3 c = LoadClamped(pos);

    // --- Deband: replace with neighborhood average where the gradient is
    // shallow (all taps within threshold), plus a sub-LSB dither. ---
    if (DebandStrength > 0.0) {
        const float radius = 8.0 * DebandStrength;
        const float threshold = 0.010 * DebandStrength;
        const float angle = Hash(float2(pos) + FrameIndex * 0.37) * 6.2831853;
        float2 d0;
        sincos(angle, d0.y, d0.x);
        const float2 d1 = float2(-d0.y, d0.x);

        float3 s0 = LoadClamped(pos + int2(d0 * radius));
        float3 s1 = LoadClamped(pos - int2(d0 * radius));
        float3 s2 = LoadClamped(pos + int2(d1 * radius));
        float3 s3 = LoadClamped(pos - int2(d1 * radius));
        float3 avg = (s0 + s1 + s2 + s3) * 0.25;

        float3 diff = max(max(abs(s0 - c), abs(s1 - c)), max(abs(s2 - c), abs(s3 - c)));
        if (max(diff.r, max(diff.g, diff.b)) < threshold) {
            float dither = (Hash(float2(pos) * 1.613 + FrameIndex) - 0.5) / 255.0;
            c = avg + dither;
        }
    }

    // --- Clarity: luma-preserving local contrast at Gaussian-blur scale.
    // Boosts mid-frequency relief (fabric, stone, carpet pile) that reads as
    // depth. Soft-knee on the delta suppresses halos around strong edges. ---
    if (Clarity > 0.0) {
        const float3 blur = BlurTexture.Load(int3(pos, 0)).rgb;
        const float lumaC = dot(c, kLuma);
        const float lumaB = dot(blur, kLuma);
        float delta = lumaC - lumaB;
        delta = delta / (1.0 + 3.0 * abs(delta));
        const float boosted = max(lumaC + Clarity * delta, 0.0);
        c *= boosted / max(lumaC, 1e-3);
    }

    // --- Bloom: add the blurred bright-pass back (light bleeds outward) ---
    if (Bloom > 0.0) {
        const float2 uv = (float2(pos) + 0.5) / OutSize;
        c += BloomTexture.SampleLevel(LinearSampler, uv, 0).rgb * Bloom;
    }

    // --- Color grade ---
    c *= exp2(Exposure);
    c = (c - 0.5) * Contrast + 0.5;
    c = pow(saturate(c), 1.0 / max(Gamma, 0.01));

    const float luma = dot(c, kLuma);
    c = lerp(luma.xxx, c, Saturation);
    // Vibrance: boost saturation more on muted pixels, protect skin/saturated areas.
    const float satAmount = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
    c = lerp(luma.xxx, c, 1.0 + Vibrance * (1.0 - saturate(satAmount * 2.0)));

    c = lerp(c, c * c * (3.0 - 2.0 * c), Filmic);

    if (Vignette > 0.0) {
        const float2 ndc = (float2(pos) + 0.5) / OutSize * 2.0 - 1.0;
        c *= 1.0 - Vignette * 0.35 * dot(ndc, ndc);
    }

    if (Grain > 0.0) {
        const float noise = Hash(float2(pos) * 0.731 + FrameIndex * 1.618) - 0.5;
        c += noise * Grain * 0.08;
    }

    OutputTexture[pos] = float4(saturate(c), 1.0);
}
