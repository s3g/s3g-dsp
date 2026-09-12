#pragma once

namespace s3g::energy {
inline constexpr const char* d3dShader = R"S3G_HLSL(
// Direct3D 11 Shader Model 5 translation of metal_shader.h.
struct Params {
  uint width; uint height; uint snapshotCount; uint activeChannels;
  uint bodyChannels; uint mapMode; uint resetHistory; uint reserved;
  float inverseFullRms; float inverseBodyRms; float activity; float directionFocus;
  float motionX; float motionY; float bodyAttack; float bodyRelease;
  float detailAttack; float detailRelease; float wakeDecay; float wakeGain;
};
cbuffer Constants : register(b0) { Params p; };
StructuredBuffer<float> basis : register(t0);
StructuredBuffer<float> snapshots : register(t1);
StructuredBuffer<float> weights : register(t2);
Texture2D<float4> previous : register(t3);
RWTexture2D<float4> next : register(u0);
Texture2D<float4> tex : register(t4);
SamplerState linearClamp : register(s0);
struct Out { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Out vertexMain(uint vid : SV_VertexID) {
  float2 pos[4] = { float2(-1.0, 1.0), float2(1.0, 1.0), float2(-1.0, -1.0), float2(1.0, -1.0) };
  float2 uv[4] = { float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0), float2(1.0, 1.0) };
  Out result; result.position = float4(pos[vid], 0.0, 1.0); result.uv = uv[vid]; return result;
}
float3 palette(float value, uint mode) {
  value = clamp(value, 0.0, 1.0);
  if (mode == 1) {
    if (value < 0.30) return lerp(float3(0.0,0.0,0.0), float3(0.063,0.204,0.408), value/0.30);
    if (value < 0.62) return lerp(float3(0.063,0.204,0.408), float3(0.133,0.627,0.808), (value-0.30)/0.32);
    return lerp(float3(0.133,0.627,0.808), float3(0.863,0.965,1.0), (value-0.62)/0.38);
  }
  if (mode == 2) {
    if (value < 0.32) return lerp(float3(0.0,0.0,0.0), float3(0.188,0.204,0.220), value/0.32);
    if (value < 0.70) return lerp(float3(0.188,0.204,0.220), float3(0.604,0.627,0.643), (value-0.32)/0.38);
    return lerp(float3(0.604,0.627,0.643), float3(0.949,0.949,0.933), (value-0.70)/0.30);
  }
  if (mode == 3) {
    if (value < 0.24) return lerp(float3(0.0,0.0,0.0), float3(0.165,0.125,0.408), value/0.24);
    if (value < 0.54) return lerp(float3(0.165,0.125,0.408), float3(0.0,0.831,0.839), (value-0.24)/0.30);
    if (value < 0.78) return lerp(float3(0.0,0.831,0.839), float3(0.933,0.925,0.384), (value-0.54)/0.24);
    return lerp(float3(0.933,0.925,0.384), float3(1.0,1.0,1.0), (value-0.78)/0.22);
  }
  if (mode == 4) {
    if (value < 0.22) return lerp(float3(0.165,0.275,0.451), float3(0.286,0.565,0.671), value/0.22);
    if (value < 0.48) return lerp(float3(0.286,0.565,0.671), float3(0.835,0.659,0.302), (value-0.22)/0.26);
    if (value < 0.72) return lerp(float3(0.835,0.659,0.302), float3(0.878,0.424,0.282), (value-0.48)/0.24);
    return lerp(float3(0.878,0.424,0.282), float3(0.973,0.878,0.494), (value-0.72)/0.28);
  }
  if (mode == 5) {
    if (value < 0.24) return lerp(float3(0.0,0.0,0.016), float3(0.341,0.059,0.427), value/0.24);
    if (value < 0.48) return lerp(float3(0.341,0.059,0.427), float3(0.733,0.216,0.329), (value-0.24)/0.24);
    if (value < 0.73) return lerp(float3(0.733,0.216,0.329), float3(0.976,0.557,0.031), (value-0.48)/0.25);
    return lerp(float3(0.976,0.557,0.031), float3(0.988,1.0,0.643), (value-0.73)/0.27);
  }
  if (mode == 6) {
    if (value < 0.25) return lerp(float3(0.267,0.004,0.329), float3(0.231,0.322,0.545), value/0.25);
    if (value < 0.50) return lerp(float3(0.231,0.322,0.545), float3(0.129,0.569,0.549), (value-0.25)/0.25);
    if (value < 0.75) return lerp(float3(0.129,0.569,0.549), float3(0.369,0.788,0.384), (value-0.50)/0.25);
    return lerp(float3(0.369,0.788,0.384), float3(0.992,0.906,0.145), (value-0.75)/0.25);
  }
  if (mode == 7) {
    if (value < 0.25) return lerp(float3(0.0,0.0,0.016), float3(0.314,0.071,0.482), value/0.25);
    if (value < 0.50) return lerp(float3(0.314,0.071,0.482), float3(0.718,0.216,0.475), (value-0.25)/0.25);
    if (value < 0.75) return lerp(float3(0.718,0.216,0.475), float3(0.984,0.533,0.380), (value-0.50)/0.25);
    return lerp(float3(0.984,0.533,0.380), float3(0.988,0.992,0.749), (value-0.75)/0.25);
  }
  if (value < 0.26) return lerp(float3(0.094,0.141,0.306), float3(0.133,0.502,0.667), value/0.26);
  if (value < 0.52) return lerp(float3(0.133,0.502,0.667), float3(0.886,0.839,0.400), (value-0.26)/0.26);
  if (value < 0.76) return lerp(float3(0.886,0.839,0.400), float3(0.886,0.416,0.243), (value-0.52)/0.24);
  return lerp(float3(0.886,0.416,0.243), float3(0.808,0.149,0.157), (value-0.76)/0.24);
}
[numthreads(16, 8, 1)]
void analysisMain(uint3 threadId : SV_DispatchThreadID) {
  uint2 gid = threadId.xy;
  if (gid.x >= p.width || gid.y >= p.height) return;
  float bodySq = 0.0; float detailSq = 0.0;
  uint basisOffset = (gid.y * p.width + gid.x) * 64;
  for (uint s = 0; s < p.snapshotCount; ++s) {
    float body = 0.0; float detail = 0.0; uint sampleOffset = s * 64;
    for (uint ch = 0; ch < p.activeChannels; ++ch) {
      float coefficient = snapshots[sampleOffset + ch];
      float decoded = coefficient * basis[basisOffset + ch];
      detail += decoded * weights[ch];
      if (ch < p.bodyChannels) body += decoded * weights[64 + ch];
    }
    bodySq += body * body; detailSq += detail * detail;
  }
  float count = max(1.0, float(p.snapshotCount));
  float body = sqrt(bodySq / count) * p.inverseBodyRms;
  float detail = sqrt(detailSq / count) * p.inverseFullRms;
  float bodyTarget = (1.0 - exp(-body * 0.95)) * p.activity;
  float detailTarget = (1.0 - exp(-detail * 0.72)) * p.activity;
  float shiftXF = clamp(p.motionX * float(p.width) * 0.55, -2.0, 2.0);
  int shiftX = int(sign(shiftXF) * floor(abs(shiftXF) + 0.5));
  float shiftYF = clamp(p.motionY * float(p.height) * 0.55, -2.0, 2.0);
  int shiftY = int(sign(shiftYF) * floor(abs(shiftYF) + 0.5));
  int oldX = (int(gid.x) - shiftX) % int(p.width); if (oldX < 0) oldX += int(p.width);
  int oldY = clamp(int(gid.y) - shiftY, 0, int(p.height) - 1);
  float4 history = p.resetHistory != 0 ? float4(0.0,0.0,0.0,0.0) : previous.Load(int3(gid, 0));
  float wakeHistory = p.resetHistory != 0 ? 0.0 : previous.Load(int3(oldX, oldY, 0)).b;
  float bodyMix = bodyTarget > history.r ? p.bodyAttack : p.bodyRelease;
  float detailMix = detailTarget > history.g ? p.detailAttack : p.detailRelease;
  float bodySmooth = lerp(history.r, bodyTarget, bodyMix);
  float detailSmooth = lerp(history.g, detailTarget, detailMix);
  float departed = max(history.g - detailTarget, 0.0) + max(history.r - bodyTarget, 0.0) * 0.36;
  float wake = max(wakeHistory * p.wakeDecay, departed * p.wakeGain);
  next[gid] = float4(bodySmooth, detailSmooth, clamp(wake,0.0,1.0), p.activity);
}
float4 fragmentMain(Out input) : SV_Target {
  float2 uv = float2(input.uv.x, 1.0 - input.uv.y);
  float2 texel = 1.0 / float2(p.width, p.height);
  float4 field = tex.Sample(linearClamp, uv);
  float body = field.r; float detail = field.g; float wake = field.b;
  float contrast = max(detail - body * 0.42, 0.0);
  float value = clamp(body * 0.78 + detail * (0.42 + p.directionFocus * 0.16) + contrast * (0.22 + p.directionFocus * 0.28), 0.0, 1.0);
  float dL = tex.Sample(linearClamp, uv - float2(texel.x,0.0)).g; float dR = tex.Sample(linearClamp, uv + float2(texel.x,0.0)).g;
  float dD = tex.Sample(linearClamp, uv - float2(0.0,texel.y)).g; float dU = tex.Sample(linearClamp, uv + float2(0.0,texel.y)).g;
  float ridge = smoothstep(0.018, 0.16, length(float2(dR-dL,dU-dD))) * detail;
  float wakeOnly = max(wake - max(body, detail) * 0.46, 0.0);
  if (value < 0.0015 && wakeOnly < 0.002) return float4(0.0,0.0,0.0,1.0);
  float3 color = palette(pow(value, 0.76), p.mapMode);
  color += ridge * (0.10 + p.directionFocus * 0.16);
  float wakeAlpha = smoothstep(0.015, 0.52, wakeOnly) * 0.68;
  float3 wakeColor = 0.12 + (1.0 - palette(pow(clamp(wake,0.0,1.0),0.70), p.mapMode)) * 0.82;
  color = lerp(color, wakeColor, wakeAlpha);
  color = pow(clamp(color * 1.06, 0.0, 1.0), float3(0.90,0.90,0.90));
  return float4(color, 1.0);
}
)S3G_HLSL";
}
