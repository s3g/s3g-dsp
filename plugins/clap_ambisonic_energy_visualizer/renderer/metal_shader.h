#pragma once

// Extracted verbatim from the shipping Cocoa/Metal renderer. This is also used
// by the offscreen parity harness; do not substitute the legacy CPU fallback.
namespace s3g::energy {
inline constexpr const char* metalShader = R"S3G_METAL(#include <metal_stdlib>
using namespace metal;
struct Params {
  uint width; uint height; uint snapshotCount; uint activeChannels;
  uint bodyChannels; uint mapMode; uint resetHistory; uint reserved;
  float inverseFullRms; float inverseBodyRms; float activity; float directionFocus;
  float motionX; float motionY; float bodyAttack; float bodyRelease;
  float detailAttack; float detailRelease; float wakeDecay; float wakeGain;
};
struct Out { float4 position [[position]]; float2 uv; };
vertex Out vertexMain(uint vid [[vertex_id]]) {
  float2 pos[4] = { float2(-1.0, 1.0), float2(1.0, 1.0), float2(-1.0, -1.0), float2(1.0, -1.0) };
  float2 uv[4] = { float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0), float2(1.0, 1.0) };
  Out out; out.position = float4(pos[vid], 0.0, 1.0); out.uv = uv[vid]; return out;
}
float3 palette(float value, uint mode) {
  value = clamp(value, 0.0, 1.0);
  if (mode == 1) {
    if (value < 0.30) return mix(float3(0.0), float3(0.063,0.204,0.408), value/0.30);
    if (value < 0.62) return mix(float3(0.063,0.204,0.408), float3(0.133,0.627,0.808), (value-0.30)/0.32);
    return mix(float3(0.133,0.627,0.808), float3(0.863,0.965,1.0), (value-0.62)/0.38);
  }
  if (mode == 2) {
    if (value < 0.32) return mix(float3(0.0), float3(0.188,0.204,0.220), value/0.32);
    if (value < 0.70) return mix(float3(0.188,0.204,0.220), float3(0.604,0.627,0.643), (value-0.32)/0.38);
    return mix(float3(0.604,0.627,0.643), float3(0.949,0.949,0.933), (value-0.70)/0.30);
  }
  if (mode == 3) {
    if (value < 0.24) return mix(float3(0.0), float3(0.165,0.125,0.408), value/0.24);
    if (value < 0.54) return mix(float3(0.165,0.125,0.408), float3(0.0,0.831,0.839), (value-0.24)/0.30);
    if (value < 0.78) return mix(float3(0.0,0.831,0.839), float3(0.933,0.925,0.384), (value-0.54)/0.24);
    return mix(float3(0.933,0.925,0.384), float3(1.0), (value-0.78)/0.22);
  }
  if (mode == 4) {
    if (value < 0.22) return mix(float3(0.165,0.275,0.451), float3(0.286,0.565,0.671), value/0.22);
    if (value < 0.48) return mix(float3(0.286,0.565,0.671), float3(0.835,0.659,0.302), (value-0.22)/0.26);
    if (value < 0.72) return mix(float3(0.835,0.659,0.302), float3(0.878,0.424,0.282), (value-0.48)/0.24);
    return mix(float3(0.878,0.424,0.282), float3(0.973,0.878,0.494), (value-0.72)/0.28);
  }
  if (mode == 5) {
    if (value < 0.24) return mix(float3(0.0,0.0,0.016), float3(0.341,0.059,0.427), value/0.24);
    if (value < 0.48) return mix(float3(0.341,0.059,0.427), float3(0.733,0.216,0.329), (value-0.24)/0.24);
    if (value < 0.73) return mix(float3(0.733,0.216,0.329), float3(0.976,0.557,0.031), (value-0.48)/0.25);
    return mix(float3(0.976,0.557,0.031), float3(0.988,1.0,0.643), (value-0.73)/0.27);
  }
  if (mode == 6) {
    if (value < 0.25) return mix(float3(0.267,0.004,0.329), float3(0.231,0.322,0.545), value/0.25);
    if (value < 0.50) return mix(float3(0.231,0.322,0.545), float3(0.129,0.569,0.549), (value-0.25)/0.25);
    if (value < 0.75) return mix(float3(0.129,0.569,0.549), float3(0.369,0.788,0.384), (value-0.50)/0.25);
    return mix(float3(0.369,0.788,0.384), float3(0.992,0.906,0.145), (value-0.75)/0.25);
  }
  if (mode == 7) {
    if (value < 0.25) return mix(float3(0.0,0.0,0.016), float3(0.314,0.071,0.482), value/0.25);
    if (value < 0.50) return mix(float3(0.314,0.071,0.482), float3(0.718,0.216,0.475), (value-0.25)/0.25);
    if (value < 0.75) return mix(float3(0.718,0.216,0.475), float3(0.984,0.533,0.380), (value-0.50)/0.25);
    return mix(float3(0.984,0.533,0.380), float3(0.988,0.992,0.749), (value-0.75)/0.25);
  }
  if (value < 0.26) return mix(float3(0.094,0.141,0.306), float3(0.133,0.502,0.667), value/0.26);
  if (value < 0.52) return mix(float3(0.133,0.502,0.667), float3(0.886,0.839,0.400), (value-0.26)/0.26);
  if (value < 0.76) return mix(float3(0.886,0.839,0.400), float3(0.886,0.416,0.243), (value-0.52)/0.24);
  return mix(float3(0.886,0.416,0.243), float3(0.808,0.149,0.157), (value-0.76)/0.24);
}
kernel void analysisMain(device const float* basis [[buffer(0)]],
                         device const float* snapshots [[buffer(1)]],
                         constant Params& p [[buffer(2)]],
                         device const float* weights [[buffer(3)]],
                         texture2d<half, access::read> previous [[texture(0)]],
                         texture2d<half, access::write> next [[texture(1)]],
                         uint2 gid [[thread_position_in_grid]]) {
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
  int shiftX = int(round(clamp(p.motionX * float(p.width) * 0.55, -2.0, 2.0)));
  int shiftY = int(round(clamp(p.motionY * float(p.height) * 0.55, -2.0, 2.0)));
  int oldX = (int(gid.x) - shiftX) % int(p.width); if (oldX < 0) oldX += int(p.width);
  int oldY = clamp(int(gid.y) - shiftY, 0, int(p.height) - 1);
  float4 history = p.resetHistory != 0 ? float4(0.0) : float4(previous.read(gid));
  float wakeHistory = p.resetHistory != 0 ? 0.0 : float(previous.read(uint2(uint(oldX), uint(oldY))).b);
  float bodyMix = bodyTarget > history.r ? p.bodyAttack : p.bodyRelease;
  float detailMix = detailTarget > history.g ? p.detailAttack : p.detailRelease;
  float bodySmooth = mix(history.r, bodyTarget, bodyMix);
  float detailSmooth = mix(history.g, detailTarget, detailMix);
  float departed = max(history.g - detailTarget, 0.0) + max(history.r - bodyTarget, 0.0) * 0.36;
  float wake = max(wakeHistory * p.wakeDecay, departed * p.wakeGain);
  next.write(half4(half(bodySmooth), half(detailSmooth), half(clamp(wake,0.0,1.0)), half(p.activity)), gid);
}
fragment float4 fragmentMain(Out in [[stage_in]], texture2d<float> tex [[texture(0)]], constant Params& p [[buffer(0)]]) {
  constexpr sampler s(address::clamp_to_edge, filter::linear);
  float2 uv = float2(in.uv.x, 1.0 - in.uv.y);
  float2 texel = 1.0 / float2(tex.get_width(), tex.get_height());
  float4 field = tex.sample(s, uv);
  float body = field.r; float detail = field.g; float wake = field.b;
  float contrast = max(detail - body * 0.42, 0.0);
  float value = clamp(body * 0.78 + detail * (0.42 + p.directionFocus * 0.16) + contrast * (0.22 + p.directionFocus * 0.28), 0.0, 1.0);
  float dL = tex.sample(s, uv - float2(texel.x,0.0)).g; float dR = tex.sample(s, uv + float2(texel.x,0.0)).g;
  float dD = tex.sample(s, uv - float2(0.0,texel.y)).g; float dU = tex.sample(s, uv + float2(0.0,texel.y)).g;
  float ridge = smoothstep(0.018, 0.16, length(float2(dR-dL,dU-dD))) * detail;
  float wakeOnly = max(wake - max(body, detail) * 0.46, 0.0);
  if (value < 0.0015 && wakeOnly < 0.002) return float4(0.0,0.0,0.0,1.0);
  float3 color = palette(pow(value, 0.76), p.mapMode);
  color += ridge * (0.10 + p.directionFocus * 0.16);
  float wakeAlpha = smoothstep(0.015, 0.52, wakeOnly) * 0.68;
  float3 wakeColor = 0.12 + (1.0 - palette(pow(clamp(wake,0.0,1.0),0.70), p.mapMode)) * 0.82;
  color = mix(color, wakeColor, wakeAlpha);
  color = pow(clamp(color * 1.06, 0.0, 1.0), float3(0.90));
  return float4(color, 1.0);
}
)S3G_METAL";
}
