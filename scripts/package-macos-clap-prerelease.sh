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
  "$src_root/clap_loop_processor/s3g_loop_processor.clap"
  "$src_root/clap_macro_delay/s3g_macro_delay.clap"
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
EOF

(cd "$dist_root" && zip -qry "$zip_path" "$package_name")
echo "$zip_path"
