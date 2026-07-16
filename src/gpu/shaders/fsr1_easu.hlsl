// FSR 1.0 EASU (Edge Adaptive Spatial Upsampling) compute wrapper.
// Algorithm implementation lives in AMD's MIT-licensed ffx_fsr1.h (included below);
// this file only provides the resource bindings and thread dispatch shell.

#define A_GPU 1
#define A_HLSL 1
#include "ffx_a.h"

Texture2D InputTexture : register(t0);
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> OutputTexture : register(u0);

cbuffer EasuConstants : register(b0) {
    uint4 Const0;
    uint4 Const1;
    uint4 Const2;
    uint4 Const3;
};

#define FSR_EASU_F 1
AF4 FsrEasuRF(AF2 p) { return InputTexture.GatherRed(LinearSampler, p, int2(0, 0)); }
AF4 FsrEasuGF(AF2 p) { return InputTexture.GatherGreen(LinearSampler, p, int2(0, 0)); }
AF4 FsrEasuBF(AF2 p) { return InputTexture.GatherBlue(LinearSampler, p, int2(0, 0)); }
#include "ffx_fsr1.h"

void Filter(AU2 pos) {
    AF3 c;
    FsrEasuF(c, pos, Const0, Const1, Const2, Const3);
    OutputTexture[pos] = float4(c, 1.0);
}

// Standard FSR dispatch: 64 threads remapped to an 8x8 quad, each thread
// shades 4 pixels in a 16x16 tile. Dispatch ((outW+15)/16, (outH+15)/16, 1).
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
