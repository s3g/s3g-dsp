#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src_root="$repo_root/build-clap/plugins"
dist_root="$repo_root/dist"
package_name="${1:-s3g-dsp-macos-clap-pre-release}"
staging="$dist_root/$package_name"
zip_path="$dist_root/$package_name.zip"

bundles=(
  "$src_root/clap_24ch_passthrough/s3g_24ch_passthrough_test.clap"
  "$src_root/clap_delay_processor/s3g_8ch_delay_processor.clap"
  "$src_root/clap_delay_processor/s3g_24ch_delay_processor.clap"
  "$src_root/clap_mc_to_stereo_autogain/s3g_mc_to_stereo_autogain.clap"
  "$src_root/clap_3oafx_boundary/s3g_3oafx_boundary.clap"
  "$src_root/clap_3oafx_point_encoder/s3g_3oafx_point_encoder.clap"
  "$src_root/clap_3oafx_speaker_decoder/s3g_3oafx_speaker_decoder.clap"
  "$src_root/clap_layout_panner/s3g_layout_panner.clap"
  "$src_root/clap_loop_processor/s3g_loop_processor.clap"
  "$src_root/clap_multi_loop_processor/s3g_multi_loop_processor.clap"
  "$src_root/clap_macro_delay/s3g_macro_delay.clap"
  "$src_root/clap_macro_delay/s3g_24ch_macro_delay.clap"
  "$src_root/clap_macro_pitch/s3g_macro_pitch.clap"
  "$src_root/clap_macro_pitch/s3g_24ch_macro_pitch.clap"
  "$src_root/clap_multichannel_meter/s3g_multichannel_meter.clap"
  "$src_root/clap_ambisonic_energy_visualizer/s3g_ambisonic_energy_visualizer.clap"
  "$src_root/clap_ambisonic_stereo_decoder/s3g_ambisonic_stereo_decoder.clap"
  "$src_root/clap_ambisonic_head_decoder/s3g_ambisonic_head_decoder.clap"
  "$src_root/clap_ambisonic_rotate/s3g_ambisonic_rotate.clap"
  "$src_root/clap_ambisonic_order_band_tool/s3g_ambisonic_order_band_tool.clap"
)

for bundle in "${bundles[@]}"; do
  if [[ ! -d "$bundle" ]]; then
    echo "Missing built bundle: $bundle" >&2
    echo "Run: cmake --preset clap && cmake --build --preset clap" >&2
    exit 1
  fi
done

rm -rf "$staging" "$zip_path"
mkdir -p "$staging"

for bundle in "${bundles[@]}"; do
  cp -R "$bundle" "$staging/"
done

cat > "$staging/README.txt" <<'EOF'
s3g-dsp pre-release macOS CLAP builds for REAPER testing.

These binaries are provided for early testing only. Plugin names, parameter
mappings, state compatibility, and the included plugin set may change before a
stable release.

Install by copying the .clap bundles to:

~/Library/Audio/Plug-Ins/CLAP/

Then rescan CLAP plugins in REAPER.

Included plugins:

- s3g 24ch Passthrough Test
- s3g Delay Processor 8ch
- s3g Delay Processor 24ch
- s3g MC to Stereo Autogain
- s3g 3OAFX Send Decoder / Return Encoder
- s3g 3OAFX Point Encoder
- s3g 3OAFX Speaker Decoder 64
- s3g Layout Panner
- s3g Loop Processor 8ch
- s3g Multi Loop Processor 8ch
- s3g Macro Delay 8ch
- s3g Macro Delay 24ch
- s3g Macro Pitch 8ch
- s3g Macro Pitch 24ch
- s3g Multichannel Meter 64
- s3g Ambisonic Energy Visualizer 64
- s3g Ambisonic Stereo Decoder
- s3g Ambisonic Head Decoder
- s3g Ambisonic Rotate 64
- s3g Ambisonic Order / Band Tool 64

Docs:

https://s3g.github.io/s3g-dsp/
EOF

(cd "$dist_root" && zip -qry "$zip_path" "$package_name")
echo "$zip_path"
