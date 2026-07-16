// UI theme pass: holo-styled frames drawn AROUND the game's action bars
// (positions from the GUIProfiles layout XML). Slot interiors are never
// painted — only the outer frame, glow, inter-slot seams and corner ticks.

Texture2D InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

#define MAX_RECTS 16

cbuffer ThemeConstants : register(b0) {
    float2 OutSize;
    float RectCount;
    float Intensity;
    float4 Rects[MAX_RECTS]; // x, y, w, h (output pixels)
    float4 Grids[MAX_RECTS]; // cols, rows, cellW, cellH
};

static const float3 kGold = float3(1.00, 0.76, 0.30);
static const float3 kCyan = float3(0.20, 0.78, 1.00);

float sdRoundBox(float2 p, float2 b, float r) {
    float2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

[numthreads(8, 8, 1)]
void mainCS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)OutSize.x || id.y >= (uint)OutSize.y) return;

    float3 c = InputTexture.Load(int3(id.xy, 0)).rgb;
    const float2 p = float2(id.xy) + 0.5;

    [loop]
    for (int i = 0; i < (int)RectCount && i < MAX_RECTS; ++i) {
        const float4 rc = Rects[i];
        const float2 center = rc.xy + rc.zw * 0.5;
        const float pad = 5.0;

        const float d = sdRoundBox(p - center, rc.zw * 0.5 + pad, 9.0);

        // vertical gradient: cyan top -> gold bottom (holo look)
        const float tGrad = saturate((p.y - rc.y) / max(rc.w, 1.0));
        const float3 frameCol = lerp(kCyan, kGold, tGrad);

        // soft outer glow
        if (d > 0.0) {
            c += frameCol * exp(-d * 0.20) * 0.28 * Intensity;
        }
        // crisp border band
        const float border = 1.0 - smoothstep(1.4, 2.6, abs(d));
        c = lerp(c, frameCol, border * 0.85 * Intensity);

        // inter-slot seams + corner ticks (in the gaps AROUND abilities,
        // never over slot interiors)
        const float4 g = Grids[i];
        if (g.x >= 1.0 && d < -2.5) {
            const float2 local = p - rc.xy;
            const float2 cell = float2(g.z, g.w);
            const float2 inCell = fmod(local, cell);
            const float2 distEdge = min(inCell, cell - inCell);

            const float seam = 1.0 - smoothstep(0.7, 1.6, min(distEdge.x, distEdge.y));
            c = lerp(c, frameCol * 0.6, seam * 0.5 * Intensity);

            // corner tick: small bright L where two seams meet
            const float m = max(distEdge.x, distEdge.y);
            if (distEdge.x < 6.0 && distEdge.y < 6.0) {
                const float tick = 1.0 - smoothstep(4.0, 6.0, m);
                c = lerp(c, kGold, tick * 0.7 * Intensity);
            }
        }
    }

    OutputTexture[id.xy] = float4(saturate(c), 1.0);
}
