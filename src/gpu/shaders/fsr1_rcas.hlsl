// FSR 1.0 RCAS (Robust Contrast Adaptive Sharpening) compute wrapper.
// Runs as a second pass on EASU output, at output resolution.

#define A_GPU 1
#define A_HLSL 1
#include "ffx_a.h"

Texture2D InputTexture : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer RcasConstants : register(b0) {
    uint4 Const0;
};

#define FSR_RCAS_F 1
AF4 FsrRcasLoadF(ASU2 p) { return InputTexture.Load(int3(p, 0)); }
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
#include "ffx_fsr1.h"

void Filter(AU2 pos) {
    AF3 c;
    FsrRcasF(c.r, c.g, c.b, pos, Const0);
    OutputTexture[pos] = float4(c, 1.0);
}

[numthreads(64, 1, 1)]
void mainCS(uint3 LocalThreadId : SV_GroupThreadID, uint3 WorkGroupId : SV_GroupID) {
    AU2 gxy = ARmp8x8(LocalThreadId.x) + AU2(WorkGroupId.x << 4u, WorkGroupId.y << 4u);
    Filter(gxy);
    gxy.x += 8u;
    Filter(gxy);
    gxy.y += 8u;
    Filter(gxy);
    gxy.x -= 8u;
    Filter(gxy);
}
