#!/usr/bin/env bash
set -euo pipefail

# Each run publishes a fresh folder/zip; never replace an earlier test package.
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${S3G_WINDOWS_CLAP_BUILD_DIR:-$repo_root/build-clap-windows-cross}"
if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
  echo "Configure the Windows build first: cmake --preset clap-windows-cross" >&2
  exit 1
fi
if ! rg -q '^set\(CMAKE_SYSTEM_NAME "Windows"\)' "$build_dir/CMakeFiles/"*/CMakeSystem.cmake; then
  echo "Not a configured Windows build: $build_dir" >&2
  exit 1
fi
cmake -S "$repo_root" -B "$build_dir" \
  -DS3G_BUILD_DRUM_GUI_PILOTS=ON -DS3G_ENABLE_PORTABLE_CLAP_GUI=ON
cmake --build "$build_dir" --target s3g_drum_kick_clap s3g_drum_hi_hat_clap -j 6

mkdir -p "$repo_root/dist"
staging="$(mktemp -d "$repo_root/dist/s3g-dsp-windows-drum-pilots-x64.XXXXXX")"
mkdir -p "$staging/Resources/Fonts"
for drum in kick hi_hat; do
  source_binary="$build_dir/plugins/clap_drum_$drum/s3g_drum_$drum.clap"
  file "$source_binary" | rg -q 'PE32\+ executable.*x86-64'
  imports="$(x86_64-w64-mingw32-objdump -p "$source_binary")"
  if [[ "$imports" != *clap_entry* ]] || \
      rg -q 'DLL Name: (libgcc|libstdc\+\+|libwinpthread)' <<< "$imports"; then
    echo "Invalid CLAP export or external MinGW runtime: $source_binary" >&2
    exit 1
  fi
  cp "$source_binary" "$staging/s3g_drum_${drum}_2.clap"
done
cp "$repo_root/assets/fonts/FiraCode-Regular.ttf" "$staging/Resources/Fonts/"
cp "$repo_root/assets/fonts/FiraCode-LICENSE.txt" "$staging/Resources/Fonts/"
cp "$repo_root/plugins/common/DRUM_VSTGUI_WINDOWS_TESTING.txt" "$staging/README.txt"
cp "$repo_root/LICENSE" "$staging/LICENSE.txt"
cp "$repo_root/THIRD_PARTY_NOTICES.md" "$staging/THIRD_PARTY_NOTICES.md"
package_name="$(basename "$staging")"
(cd "$repo_root/dist" && zip -qr "$package_name.zip" "$package_name")
echo "Windows Drum pilots: $staging.zip"
