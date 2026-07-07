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

remove_stale_bundle() {
  local name="$1"
  rm -rf "$dst_root/$name"
}

remove_stale_bundle "s3g_loop_field.clap"
remove_stale_bundle "s3g_utility_delay.clap"
remove_stale_bundle "s3g_modal_terrain.clap"
remove_stale_bundle "s3g_resonant_terrain.clap"
remove_stale_bundle "s3g_string_terrain.clap"

copy_bundle "$src_root/clap_24ch_passthrough/s3g_24ch_passthrough_test.clap"
copy_bundle "$src_root/clap_delay_processor/s3g_8ch_delay_processor.clap"
copy_bundle "$src_root/clap_delay_processor/s3g_24ch_delay_processor.clap"
copy_bundle "$src_root/clap_mc_to_stereo_autogain/s3g_mc_to_stereo_autogain.clap"
copy_bundle "$src_root/clap_3oafx_boundary/s3g_3oafx_boundary.clap"
copy_bundle "$src_root/clap_3oafx_point_encoder/s3g_3oafx_point_encoder.clap"
copy_bundle "$src_root/clap_loop_processor/s3g_loop_processor.clap"
copy_bundle "$src_root/clap_macro_delay/s3g_macro_delay.clap"
copy_bundle "$src_root/clap_macro_pitch/s3g_macro_pitch.clap"
