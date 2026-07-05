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

copy_bundle "$src_root/clap_24ch_passthrough/s3g_24ch_passthrough_test.clap"
copy_bundle "$src_root/clap_delay_processor/s3g_8ch_delay_processor.clap"
copy_bundle "$src_root/clap_delay_processor/s3g_24ch_delay_processor.clap"
copy_bundle "$src_root/clap_mc_to_stereo_autogain/s3g_mc_to_stereo_autogain.clap"
copy_bundle "$src_root/clap_3oafx_boundary/s3g_3oafx_boundary.clap"
