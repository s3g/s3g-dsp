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
  "$src_root/clap_mc_to_quad_autogain/s3g_mc_to_quad_autogain.clap"
  "$src_root/clap_3oafx_single_effects/s3g_3oafx_delay.clap"
  "$src_root/clap_3oafx_single_effects/s3g_3oafx_pitch.clap"
  "$src_root/clap_3oafx_single_effects/s3g_3oafx_filter.clap"
  "$src_root/clap_3oafx_single_effects/s3g_3oafx_gain.clap"
  "$src_root/clap_3oafx_displacement/s3g_3oafx_displacement.clap"
  "$src_root/clap_3oafx_point_encoder/s3g_3oafx_point_encoder.clap"
  "$src_root/clap_ambi_cloud_encoder/s3g_ambi_cloud_encoder.clap"
  "$src_root/clap_ambi_terrain_navigator/s3g_ambi_terrain_navigator.clap"
  "$src_root/clap_ambi_vot_encoder/s3g_ambi_vot_encoder.clap"
  "$src_root/clap_ambi_vox_encoder/s3g_ambi_vox_encoder.clap"
  "$src_root/clap_ambi_wave_terrain_encoder/s3g_ambi_wave_terrain_encoder.clap"
  "$src_root/clap_ambi_stochastic_encoder/s3g_ambi_stochastic_encoder.clap"
  "$src_root/clap_ambi_imprint/s3g_ambi_imprint.clap"
  "$src_root/clap_ambi_path_encoder/s3g_ambi_path_encoder.clap"
  "$src_root/clap_3oafx_speaker_decoder/s3g_3oafx_speaker_decoder.clap"
  "$src_root/clap_layout_panner/s3g_layout_panner.clap"
  "$src_root/clap_dbap_panner/s3g_dbap_panner.clap"
  "$src_root/clap_lbap_panner/s3g_lbap_panner.clap"
  "$src_root/clap_vbap_panner/s3g_vbap_panner.clap"
  "$src_root/clap_sub_crossover/s3g_sub_crossover.clap"
  "$src_root/clap_loop_processor/s3g_loop_processor.clap"
  "$src_root/clap_multi_loop_processor/s3g_multi_loop_processor.clap"
  "$src_root/clap_ambi_grain_processor/s3g_ambi_grain_processor.clap"
  "$src_root/clap_macro_delay/s3g_macro_delay.clap"
  "$src_root/clap_macro_delay/s3g_24ch_macro_delay.clap"
  "$src_root/clap_macro_pitch/s3g_macro_pitch.clap"
  "$src_root/clap_macro_pitch/s3g_24ch_macro_pitch.clap"
  "$src_root/clap_buffer_processor/s3g_buffer_processor.clap"
  "$src_root/clap_wave_geometry_processor/s3g_wave_geometry_processor.clap"
  "$src_root/clap_multichannel_meter/s3g_multichannel_meter.clap"
  "$src_root/clap_ambisonic_energy_visualizer/s3g_ambisonic_energy_visualizer.clap"
  "$src_root/clap_ambisonic_stereo_decoder/s3g_ambisonic_stereo_decoder.clap"
  "$src_root/clap_ambisonic_head_decoder/s3g_ambisonic_head_decoder.clap"
  "$src_root/clap_ambisonic_sub_decoder/s3g_ambisonic_sub_decoder.clap"
  "$src_root/clap_ambi_object_decoder/s3g_ambi_object_decoder.clap"
  "$src_root/clap_ambi_adaptive_decoder/s3g_ambi_adaptive_decoder.clap"
  "$src_root/clap_array_hpf/s3g_array_hpf_16.clap"
  "$src_root/clap_array_hpf/s3g_array_hpf_26.clap"
  "$src_root/clap_array_hpf/s3g_array_hpf_32.clap"
  "$src_root/clap_array_hpf/s3g_array_hpf_64.clap"
  "$src_root/clap_array_delay/s3g_array_delay_16.clap"
  "$src_root/clap_array_delay/s3g_array_delay_26.clap"
  "$src_root/clap_array_delay/s3g_array_delay_32.clap"
  "$src_root/clap_array_delay/s3g_array_delay_64.clap"
  "$src_root/clap_array_trim/s3g_array_trim_16.clap"
  "$src_root/clap_array_trim/s3g_array_trim_26.clap"
  "$src_root/clap_array_trim/s3g_array_trim_32.clap"
  "$src_root/clap_array_trim/s3g_array_trim_64.clap"
  "$src_root/clap_ambisonic_rotate/s3g_ambisonic_rotate.clap"
  "$src_root/clap_ambi_group_rotate/s3g_ambi_group_rotate_64.clap"
  "$src_root/clap_ambi_group_rotate/s3g_ambi_group_rotate_128.clap"
  "$src_root/clap_ambi_group_depth/s3g_ambi_depth_16.clap"
  "$src_root/clap_ambi_group_depth/s3g_ambi_group_depth_64.clap"
  "$src_root/clap_ambi_group_depth/s3g_ambi_group_depth_128.clap"
  "$src_root/clap_ambisonic_order_band_tool/s3g_ambisonic_order_band_tool.clap"
  "$src_root/clap_ambi_group_matrix/s3g_ambi_group_matrix.clap"
  "$src_root/clap_ambi_group_matrix_128/s3g_ambi_group_matrix_128.clap"
  "$src_root/clap_group_matrix/s3g_group_matrix.clap"
  "$src_root/clap_group_matrix_32/s3g_group_matrix_32.clap"
  "$src_root/clap_node_track_mixer/s3g_node_bus_mixer.clap"
  "$src_root/clap_node_track_mixer/s3g_ambi_node_bus_mixer.clap"
  "$src_root/clap_spectral_spray/s3g_spectral_spray.clap"
  "$src_root/clap_8ch_spectral_spray/s3g_8ch_spectral_spray.clap"
  "$src_root/clap_spectral_topology_processor/s3g_spectral_topology_processor.clap"
  "$src_root/clap_spectral_topology_processor/s3g_24ch_spectral_topology_processor.clap"
  "$src_root/clap_shard_scatter/s3g_shard_scatter.clap"
  "$src_root/clap_orbit_delay/s3g_orbit_delay.clap"
  "$src_root/clap_cascade_taps/s3g_cascade_taps.clap"
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

cp -R "$repo_root/wavetables/vot" "$staging/VOT Wavetables"
cp -R "$repo_root/examples/ambi-vox-lpc" "$staging/Ambi Vox LPC Examples"
cp "$repo_root/LICENSE" "$staging/LICENSE.txt"
cp "$repo_root/THIRD_PARTY_NOTICES.md" "$staging/THIRD_PARTY_NOTICES.md"

cat > "$staging/README.txt" <<'EOF'
s3g-dsp pre-release macOS CLAP builds for REAPER testing.

Version: 0.4.0-pre
Release date: 2026-07-15

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
- s3g MC to Quad Autogain
- s3g 3OAFX Delay
- s3g 3OAFX Pitch
- s3g 3OAFX Filter
- s3g 3OAFX Gain
- s3g 3OAFX Displacement
- s3g Ambi Point Encoder
- s3g Ambi Cloud Encoder 64
- s3g Ambi Terrain Navigator 64
- s3g Ambi VOT Encoder 64
- s3g Ambi Vox Encoder 64
- s3g Ambi Wave Terrain Encoder 64
- s3g Ambi Stochastic Encoder 64
- s3g Ambi Imprint 64
- s3g Ambi Path Encoder 64
- s3g Ambi Speaker Decoder 64
- s3g Layout Panner
- s3g DBAP Panner
- s3g LBAP Panner
- s3g VBAP Panner
- s3g Sub Crossover
- s3g Loop Processor 8ch
- s3g Multi Loop Processor 8ch
- s3g Ambi Grain Processor
- s3g Macro Delay 8ch
- s3g Macro Delay 24ch
- s3g Macro Pitch 8ch
- s3g Macro Pitch 24ch
- s3g Buffer Processor 8ch
- s3g Wave Geometry Processor 8ch
- s3g Multichannel Meter 64
- s3g Ambi Energy Visualizer 64
- s3g Ambi Stereo Decoder
- s3g Ambi Head Decoder
- s3g Ambisonic Sub Decoder
- s3g Ambi Object Decoder 64
- s3g Ambi Adaptive Decoder 64
- s3g Array HPF 16
- s3g Array HPF 26
- s3g Array HPF 32
- s3g Array HPF 64
- s3g Array Delay 16
- s3g Array Delay 26
- s3g Array Delay 32
- s3g Array Delay 64
- s3g Array Trim 16
- s3g Array Trim 26
- s3g Array Trim 32
- s3g Array Trim 64
- s3g Ambi Rotate 64
- s3g Ambi Depth 16
- s3g Ambi Group Rotate 64
- s3g Ambi Group Rotate 128
- s3g Ambi Group Depth 64
- s3g Ambi Group Depth 128
- s3g Ambi Order / Band 64
- s3g Ambi Group Matrix 64
- s3g Ambi Group Matrix 128
- s3g Group Matrix 64
- s3g Group Matrix 32
- s3g Node Bus Mixer 128
- s3g Ambi Node Bus Mixer 128
- s3g Spectral Spray 2ch
- s3g Spectral Spray 8ch
- s3g Spectral Topology Processor 8ch
- s3g Spectral Topology Processor 24ch
- s3g Shard Scatter
- s3g Orbit Delay
- s3g Cascade Taps

Docs:

https://s3g.github.io/s3g-dsp/

License:

s3g-dsp is distributed under the BSD 3-Clause License. See LICENSE.txt.
Third-party notices, including WORLD speech vocoder attribution for Ambi Vox
Encoder's WORLD WAV source path, are included in THIRD_PARTY_NOTICES.md.

VOT Wavetable Library:

Use the LOAD button in s3g Ambi VOT Encoder 64 to load any WAV file from the
included VOT Wavetables folder. The library contains twenty-eight 4 x 4 banks,
including four vocal-source atlases.

Ambi Vox LPC Examples:

Use the LOAD button in the s3g Ambi Vox Encoder 64 PHRASE panel to load .hex
files from the included Ambi Vox LPC Examples folder. These are synthetic test
files for the encoded-frame loader, not borrowed Speak & Spell ROM data.

Ambi Vox WORLD WAV Source:

When built with WORLD support, the same PHRASE panel LOAD button can load WAV
files for WORLD analysis/resynthesis before Ambi Vox spatialization.
EOF

(cd "$dist_root" && zip -qry "$zip_path" "$package_name")
echo "$zip_path"
