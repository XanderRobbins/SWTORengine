// Phase 6 frame interpolation: blend of the two most recent processed frames,
// presented between them to double perceived frame rate.

Texture2D FrameA : register(t0); // older
Texture2D FrameB : register(t1); // newer
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer BlendConstants : register(b0) {
    float BlendT; // 0 = A, 1 = B
    float _pad;
    float2 OutSize;
};

[numthreads(8, 8, 1)]
void mainCS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)OutSize.x || id.y >= (uint)OutSize.y) return;
    const int3 p = int3(id.xy, 0);
    OutputTexture[id.xy] = lerp(FrameA.Load(p), FrameB.Load(p), BlendT);
}
