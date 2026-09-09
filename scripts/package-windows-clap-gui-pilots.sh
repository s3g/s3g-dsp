#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
default_build_dir="$repo_root/build-clap-windows-cross"
build_dir="${S3G_WINDOWS_CLAP_BUILD_DIR:-$default_build_dir}"
package_name="s3g-dsp-windows-clap-gui-pilots-x64"
dist_root="$repo_root/dist"
final_staging="$dist_root/$package_name"
zip_path="$dist_root/$package_name.zip"

if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
  echo "Missing x86_64-w64-mingw32-g++. Install it with: brew install mingw-w64" >&2
  exit 1
fi

if [[ "$build_dir" == "$default_build_dir" ]]; then
  cmake --preset clap-windows-cross
elif [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
  echo "Custom Windows build directory is not configured: $build_dir" >&2
  exit 1
fi

cmake --build "$build_dir" --target \
  s3g_macro_delay_clap \
  s3g_sample_player_clap \
  s3g_sample_doubles_clap \
  s3g_sample_wavesets_clap \
  s3g_sample_motion_clap \
  s3g_sample_lanes_clap \
  s3g_sample_grains_clap \
  s3g_sample_cutups_clap \
  s3g_sample_rings_clap \
  s3g_crcltr_clap \
  s3g_ambi_point_encoder_clap \
  s3g_ambi_stochastic_encoder_clap \
  -j 8

source_paths=(
  "plugins/clap_macro_delay/s3g_macro_delay.clap"
  "plugins/clap_sample_player/s3g_sample_player.clap"
  "plugins/clap_sample_doubles/s3g_sample_doubles.clap"
  "plugins/clap_sample_wavesets/s3g_sample_wavesets.clap"
  "plugins/clap_sample_motion/s3g_sample_motion.clap"
  "plugins/clap_sample_lanes/s3g_sample_lanes.clap"
  "plugins/clap_sample_grains/s3g_sample_grains.clap"
  "plugins/clap_sample_cutups/s3g_sample_cutups.clap"
  "plugins/clap_sample_rings/s3g_sample_rings.clap"
  "plugins/clap_crcltr/s3g_crcltr.clap"
  "plugins/clap_ambi_point_encoder/s3g_ambi_point_encoder.clap"
  "plugins/clap_ambi_stochastic_encoder/s3g_ambi_stochastic_encoder.clap"
)
output_names=(
  "s3g_macro_delay_8.clap"
  "s3g_sample_player_2.clap"
  "s3g_sample_doubles_2.clap"
  "s3g_sample_wavesets_2.clap"
  "s3g_sample_motion_2.clap"
  "s3g_sample_lanes_2.clap"
  "s3g_sample_grains_2.clap"
  "s3g_sample_cutups_2.clap"
  "s3g_sample_rings_8.clap"
  "s3g_sample_circulator_2.clap"
  "s3g_ambi_encoder_point_64.clap"
  "s3g_ambi_encoder_stochastic_64.clap"
)

objdump="$(command -v x86_64-w64-mingw32-objdump)"
strip="$(command -v x86_64-w64-mingw32-strip)"

mkdir -p "$dist_root"
package_work_root="$(mktemp -d "$dist_root/.s3g-windows-pilots.XXXXXX")"
trap 'rm -rf "$package_work_root"' EXIT
staging="$package_work_root/$package_name"
mkdir -p "$staging/Resources/Fonts"

for index in "${!source_paths[@]}"; do
  source_binary="$build_dir/${source_paths[$index]}"
  staged_binary="$staging/${output_names[$index]}"
  if [[ ! -f "$source_binary" ]]; then
    echo "Missing Windows CLAP output: $source_binary" >&2
    exit 1
  fi
  if ! file "$source_binary" | grep -q "PE32+ executable.*x86-64"; then
    echo "Not a 64-bit Windows PE module: $source_binary" >&2
    exit 1
  fi
  if ! "$objdump" -p "$source_binary" | grep "clap_entry" >/dev/null; then
    echo "Missing exported clap_entry: $source_binary" >&2
    exit 1
  fi
  if "$objdump" -p "$source_binary" \
      | grep -E 'DLL Name: (libgcc|libstdc\+\+|libwinpthread)' >/dev/null; then
    echo "Unexpected MinGW runtime DLL dependency: $source_binary" >&2
    exit 1
  fi
  cp "$source_binary" "$staged_binary"
  "$strip" --strip-unneeded "$staged_binary"
done

cp "$repo_root/assets/fonts/FiraCode-Regular.ttf" \
  "$staging/Resources/Fonts/FiraCode-Regular.ttf"
cp "$repo_root/assets/fonts/FiraCode-LICENSE.txt" \
  "$staging/Resources/Fonts/FiraCode-LICENSE.txt"
cp "$repo_root/LICENSE" "$staging/LICENSE.txt"
cp "$repo_root/THIRD_PARTY_NOTICES.md" "$staging/THIRD_PARTY_NOTICES.md"

cat > "$staging/README.txt" <<'EOF'
s3g-dsp Windows x64 CLAP GUI pilots

This test package contains:

- s3g Macro Delay 8
- s3g Sample Player 2 / Sample Player 16 (two descriptors in one CLAP)
- s3g Sample Doubles 2
- s3g Sample Wavesets 2 / Sample Wavesets 32
- s3g Sample Motion 2 / Sample Motion 32
- s3g Sample Lanes 2 / Sample Lanes 32
- s3g Sample Grains 2 / Sample Grains 32
- s3g Sample Cutups 2 / Sample Cutups 32
- s3g Sample Rings 8
- s3g Sample Circulator 2
- s3g Ambi Encoder Point 64
- s3g Ambi Encoder Stochastic 64

Requirements:

- 64-bit Windows 10 or newer
- A 64-bit CLAP host such as REAPER

Manual installation:

1. Quit REAPER.
2. Copy all twelve .clap files AND the adjacent Resources folder into one of:

   Per-user:  %LOCALAPPDATA%\Programs\Common\CLAP\s3g-dsp
   All users: C:\Program Files\Common Files\CLAP\s3g-dsp

3. Restart REAPER and perform a full plug-in rescan if the new entries do not
   appear immediately.

The Resources folder contains the private Fira Code font used by every editor.
It must remain beside the .clap files; the font does not need to be
installed into Windows.

Windows Sample-family import supports WAV-family files and AIFF. Sample Player
decodes asynchronously; the other Sample-family editors decode after file
selection and immediately publish their waveform data. Editors resize
proportionally from 65% to 200% of their native dimensions; Windows text uses
slightly larger metrics for readability on standard-density displays.

These are unsigned pre-release test binaries cross-compiled on macOS. They are
intended for GUI and host-compatibility testing, not public distribution.
EOF

(
  cd "$staging"
  shasum -a 256 ./*.clap Resources/Fonts/* LICENSE.txt \
    THIRD_PARTY_NOTICES.md > SHA256SUMS.txt
)

rm -rf "$final_staging"
rm -f "$zip_path"
mv "$staging" "$final_staging"
(
  cd "$dist_root"
  zip -qry "$zip_path" "$package_name"
)

echo "Windows CLAP pilot folder: $final_staging"
echo "Windows CLAP pilot archive: $zip_path"
