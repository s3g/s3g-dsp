#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src_root="$repo_root/build-clap/plugins"
dst_root="${1:-$HOME/Library/Audio/Plug-Ins/CLAP}"

mkdir -p "$dst_root"

copy_bundle() {
  local bundle="$1"
  local name
  name="$(basename "$bundle")"
  rm -rf "$dst_root/$name"
  cp -R "$bundle" "$dst_root/$name"
}

copy_bundle "$src_root/clap_diffusion_mesh/s3g_diffusion_mesh.clap"

for bundle in "$dst_root"/s3g_*ch_diffusion_mesh.clap "$dst_root"/s3g_24ch_lane_diffusion_mesh.clap; do
  [[ -e "$bundle" ]] && rm -rf "$bundle"
done

for channels in 8 16 32 64; do
  copy_bundle "$src_root/clap_diffusion_mesh/s3g_${channels}ch_diffusion_mesh.clap"
done

copy_bundle "$src_root/clap_diffusion_mesh/s3g_24ch_lane_diffusion_mesh.clap"
copy_bundle "$src_root/clap_24ch_diffusion_mesh/s3g_24ch_diffusion_mesh.clap"
copy_bundle "$src_root/clap_24ch_passthrough/s3g_24ch_passthrough_test.clap"
rm -rf "$dst_root/s3g_8ch_tape_delay.clap"
rm -rf "$dst_root/s3g_24ch_tape_delay.clap"
rm -rf "$dst_root/s3g_8ch_filter_processor.clap" "$dst_root/s3g_8ch_spectral_field.clap"
copy_bundle "$src_root/clap_tape_delay/s3g_8ch_delay_processor.clap"
copy_bundle "$src_root/clap_tape_delay/s3g_24ch_delay_processor.clap"
copy_bundle "$src_root/clap_3oafx_boundary/s3g_3oafx_boundary.clap"
