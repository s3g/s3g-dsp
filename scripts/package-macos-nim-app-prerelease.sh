#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir_input="${S3G_APP_BUILD_DIR:-$repo_root/build-apps}"
dist_root="$repo_root/dist"
package_verifier="$repo_root/scripts/verify-macos-nim-app-package.py"
installer_source="$repo_root/scripts/install-no-input-mixer-app.sh"
app_release_notes="$repo_root/apps/no_input_mixer_standalone/RELEASE_NOTES.md"
release_version="${S3G_RELEASE_VERSION:-0.7.0-pre}"
release_date="${S3G_RELEASE_DATE:-$(date +%F)}"
codesign_identity="${S3G_CODESIGN_IDENTITY:--}"
allow_dirty="${S3G_PACKAGE_ALLOW_DIRTY:-0}"
package_name="${1:-s3g-no-input-mixer-app-macos-arm64-$release_version}"
final_staging="$dist_root/$package_name"
zip_path="$dist_root/$package_name.zip"
checksum_path="$zip_path.sha256"
app_name="s3g No Input Mixer.app"
bundle_id="org.s3g.s3g-dsp.no-input-mixer-standalone"
executable_name="s3g No Input Mixer"
app_relative_path="apps/no_input_mixer_standalone/$app_name"

codesign_args=(--force --deep --sign "$codesign_identity")
if [[ "$codesign_identity" != "-" ]]; then
  codesign_args+=(--options runtime --timestamp)
fi

if [[ ! "$package_name" =~ ^s3g-no-input-mixer-app-macos-arm64-[A-Za-z0-9._-]+$ ]]; then
  echo "Unsafe package name: $package_name" >&2
  exit 2
fi
if [[ ! "$release_version" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
  echo "Unsafe S3G_RELEASE_VERSION: $release_version" >&2
  exit 2
fi
if [[ ! "$release_date" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]]; then
  echo "S3G_RELEASE_DATE must use YYYY-MM-DD: $release_date" >&2
  exit 2
fi
if [[ "$allow_dirty" != "0" && "$allow_dirty" != "1" ]]; then
  echo "S3G_PACKAGE_ALLOW_DIRTY must be 0 or 1" >&2
  exit 2
fi
if [[ ! -x "$installer_source" ]]; then
  echo "Missing executable NIM app installer: $installer_source" >&2
  exit 1
fi
if [[ ! -x "$package_verifier" ]]; then
  echo "Missing executable NIM app package verifier: $package_verifier" >&2
  exit 1
fi
if [[ ! -f "$app_release_notes" ]]; then
  echo "Missing NIM app release notes: $app_release_notes" >&2
  exit 1
fi

if [[ ! -d "$build_dir_input" ]]; then
  echo "Missing standalone Release build directory: $build_dir_input" >&2
  echo "Configure it first with: cmake --preset apps" >&2
  exit 1
fi
build_dir="$(cd "$build_dir_input" && pwd -P)"
cache="$build_dir/CMakeCache.txt"
if [[ ! -f "$cache" ]]; then
  echo "Missing CMake cache for standalone Release build: $cache" >&2
  exit 1
fi
cache_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache")"
cache_type="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$cache")"
cache_configs="$(sed -n 's/^CMAKE_CONFIGURATION_TYPES:STRING=//p' "$cache")"
cache_apps="$(sed -n 's/^S3G_BUILD_STANDALONE_APPS:BOOL=//p' "$cache")"
if [[ -z "$cache_source" || ! -d "$cache_source" ]]; then
  echo "CMake cache has no valid source directory: $cache" >&2
  exit 1
fi
cache_source="$(cd "$cache_source" && pwd -P)"
if [[ "$cache_source" != "$repo_root" ]]; then
  echo "Standalone artifacts were configured from a different source tree:" >&2
  echo "  expected: $repo_root" >&2
  echo "  cache:    $cache_source" >&2
  exit 1
fi
if [[ "$cache_type" != "Release" && ";$cache_configs;" != *";Release;"* ]]; then
  echo "NIM app packages must come from a Release configuration: $cache" >&2
  echo "  CMAKE_BUILD_TYPE=${cache_type:-<unset>}" >&2
  echo "  CMAKE_CONFIGURATION_TYPES=${cache_configs:-<unset>}" >&2
  exit 1
fi
if [[ "$cache_apps" != "ON" ]]; then
  echo "The standalone Release tree was not configured with S3G_BUILD_STANDALONE_APPS=ON" >&2
  exit 1
fi

if [[ "$allow_dirty" -eq 0 && -n "$(git -C "$repo_root" status --porcelain --untracked-files=all)" ]]; then
  echo "Refusing to package a dirty source tree." >&2
  echo "Commit/review the release inputs, or set S3G_PACKAGE_ALLOW_DIRTY=1 for an explicitly non-final test package." >&2
  exit 1
fi

echo "Building exact standalone Release target: $build_dir"
cmake --build "$build_dir" --config Release --parallel \
  --target s3g_no_input_mixer_app
cmake --build "$build_dir" --config Release \
  --target audit_no_input_mixer_standalone

cache_project_version="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$cache")"
if [[ -z "$cache_project_version" ]]; then
  echo "Standalone CMake cache has no project version: $cache" >&2
  exit 1
fi
if [[ "$release_version" != "$cache_project_version" \
    && "$release_version" != "$cache_project_version-"* ]]; then
  echo "App package version does not match the rebuilt CMake project version:" >&2
  echo "  package: $release_version" >&2
  echo "  project: $cache_project_version" >&2
  exit 1
fi

source_app="$build_dir/$app_relative_path"
source_plist="$source_app/Contents/Info.plist"
if [[ ! -d "$source_app" || -L "$source_app" || ! -f "$source_plist" || -L "$source_plist" ]]; then
  echo "Missing or unsafe rebuilt NIM application: $source_app" >&2
  exit 1
fi
if [[ -n "$(find "$source_app" -type l -print -quit)" ]]; then
  echo "The rebuilt NIM application contains a symlink: $source_app" >&2
  exit 1
fi

plist_value() {
  /usr/libexec/PlistBuddy -c "Print :$2" "$1/Contents/Info.plist" 2>/dev/null
}

actual_id="$(plist_value "$source_app" CFBundleIdentifier)"
actual_name="$(plist_value "$source_app" CFBundleName)"
actual_type="$(plist_value "$source_app" CFBundlePackageType)"
actual_executable="$(plist_value "$source_app" CFBundleExecutable)"
app_short_version="$(plist_value "$source_app" CFBundleShortVersionString)"
app_build_version="$(plist_value "$source_app" CFBundleVersion)"
if [[ "$actual_id" != "$bundle_id" || "$actual_name" != "s3g No Input Mixer" \
    || "$actual_type" != "APPL" || "$actual_executable" != "$executable_name" ]]; then
  echo "Rebuilt NIM application metadata does not match the release identity: $source_app" >&2
  exit 1
fi
if [[ "$app_short_version" != "$cache_project_version" \
    || "$app_build_version" != "$cache_project_version" ]]; then
  echo "Rebuilt NIM application version does not match the CMake project version:" >&2
  echo "  short/build: $app_short_version / $app_build_version" >&2
  echo "  project:     $cache_project_version" >&2
  exit 1
fi

source_executable="$source_app/Contents/MacOS/$executable_name"
if [[ ! -f "$source_executable" || -L "$source_executable" || ! -x "$source_executable" ]]; then
  echo "Missing or unsafe rebuilt NIM executable: $source_executable" >&2
  exit 1
fi
architectures="$(/usr/bin/lipo -archs "$source_executable")"
if [[ "$architectures" != "arm64" ]]; then
  echo "NIM app release must be Apple-silicon-only arm64: $source_executable ($architectures)" >&2
  exit 1
fi
minimum_macos="$(/usr/bin/otool -l "$source_executable" \
  | awk '$1 == "minos" { print $2; exit }')"
if [[ ! "$minimum_macos" =~ ^[0-9]+\.[0-9]+([.][0-9]+)?$ ]]; then
  echo "Cannot determine the minimum macOS version from: $source_executable" >&2
  exit 1
fi
/usr/bin/codesign --verify --deep --strict "$source_app"

mkdir -p "$dist_root"
package_work_root="$(mktemp -d "$dist_root/.s3g-nim-app-package.XXXXXX")"
staging="$package_work_root/$package_name"
candidate_zip="$package_work_root/$package_name.zip"
candidate_checksum="$candidate_zip.sha256"
cleanup_package_work() {
  if [[ -n "${package_work_root:-}" && -d "$package_work_root" ]]; then
    rm -rf "$package_work_root"
  fi
}
trap cleanup_package_work EXIT

mkdir -p "$staging/Installer Data"
/usr/bin/ditto --noqtn "$source_app" "$staging/$app_name"
/usr/bin/codesign "${codesign_args[@]}" "$staging/$app_name"
/usr/bin/codesign --verify --deep --strict "$staging/$app_name"

cp "$installer_source" "$staging/Install s3g No Input Mixer.command"
chmod 755 "$staging/Install s3g No Input Mixer.command"
cp "$repo_root/LICENSE" "$staging/LICENSE.txt"
cp "$repo_root/THIRD_PARTY_NOTICES.md" "$staging/THIRD_PARTY_NOTICES.md"
cp "$app_release_notes" "$staging/RELEASE_NOTES.md"

git_revision="$(git -C "$repo_root" rev-parse HEAD)"
git_dirty="clean"
if [[ -n "$(git -C "$repo_root" status --porcelain --untracked-files=all)" ]]; then
  git_dirty="dirty (explicitly allowed for non-final testing)"
fi
cat > "$staging/Installer Data/build-provenance.txt" <<EOF
s3g No Input Mixer app package version: $release_version
Source revision: $git_revision
Source status: $git_dirty
CMake configuration: Release
CMake project version: $cache_project_version
Application bundle version: $app_short_version
Bundle identifier: $bundle_id
Artifact tree: $(basename "$build_dir")/$app_relative_path
Architecture: arm64
Minimum macOS: $minimum_macos
Code-signing identity: $codesign_identity
EOF

cat > "$staging/README.txt" <<EOF
s3g No Input Mixer standalone app for macOS

Version: $release_version
Release date: $release_date
Minimum macOS: $minimum_macos

Compatibility:

- Apple silicon Macs only (arm64: M1, M2, M3, M4, or newer).
- macOS $minimum_macos or newer.
- A Core Audio output with at least 2 channels for Stereo, 4 for Quad, or 8
  for Direct 8 output.
- REAPER and the separately distributed s3g-dsp CLAP package are not required.

This is pre-release software. The app is ad-hoc signed by default for bundle
integrity verification but is not Apple notarized.

Recommended installation:

1. Quit s3g No Input Mixer if it is running, then unzip this download.
2. Double-click "Install s3g No Input Mixer.command".
3. If macOS blocks the command, click OK, open
   System Settings > Privacy & Security, and choose Open Anyway. Confirm Open
   and authenticate if macOS
   asks. Open Anyway remains available for about one hour after the blocked
   launch.
4. If prompted, allow Terminal to access the downloaded folder. Wait for the
   installer to finish; it does not launch the audio application automatically.
5. Open "s3g No Input Mixer" from your user Applications folder.

Default installation:

~/Applications/s3g No Input Mixer.app

The installer works entirely in the current user's account and never uses
sudo. It validates the source app before changing the destination. On an
update, it moves only an identity-verified prior NIM app to a timestamped
backup under:

~/Library/Application Support/s3g-dsp/No Input Mixer Backups/

An unrelated app, file, or symlink at the destination is never overwritten.
The installer copies into a private staging folder, recursively removes only
com.apple.quarantine from that verified staged app, verifies it again, and
then completes the update. Other extended attributes, saved preferences,
separately installed CLAP plugins, and user-saved .nimgesture files are left
untouched.

To preview the validated installation from Terminal without changing files:

./Install\ s3g\ No\ Input\ Mixer.command --dry-run

First launch and audio safety:

1. Begin with amplifier, interface, and monitor levels low.
2. The app opens its selected Core Audio device but starts in the Safe Mute
   state, so no NIM signal is sent to hardware yet.
3. In the persistent ROUTE strip, choose STEREO (2 channels), QUAD (4
   channels), or DIRECT (8 channels), then select the intended output device
   and OUT channel bank. Unavailable modes are disabled when the device does
   not expose enough outputs.
4. Keep the NIM ceiling limiter enabled while establishing a feedback patch.
   Select AUDIO ON only after checking the complete route. PANIC remains
   available in the output strip on every NIM page.
5. EDIT OUTPUT opens the Stereo or Quad Autogain controls. Direct 8 sends the
   eight NIM lanes directly to the selected eight-channel hardware bank.

MIDI and gesture sessions:

- MIDI control is optional. Select MIDI, then enable only the connected
  CoreMIDI sources that should control NIM. The standalone uses the same E16,
  BU16, NRPN, note/velocity, and LED-feedback mappings as the CLAP processor.
- Embedded NIM Gesture recordings are intentionally transient. Unsaved loops
  are cleared when the app quits and Play is not restored on the next launch.
- Use SAVE SESSION to export a .nimgesture performance and LOAD SESSION to
  load one deliberately. A loaded session remains paused until Play is turned
  on; the last file is never reopened automatically.

Manual installation:

Use this only for a fresh install. Quit the app, open Terminal in this unzipped
package folder, and run:

  mkdir -p "\$HOME/Applications"
  /usr/bin/ditto --noqtn "./s3g No Input Mixer.app" \
    "\$HOME/Applications/s3g No Input Mixer.app"
  /usr/bin/xattr -drs com.apple.quarantine \
    "\$HOME/Applications/s3g No Input Mixer.app"

The included installer is preferred for updates because it verifies and backs
up the existing application before replacement.

Uninstalling:

1. Quit s3g No Input Mixer.
2. Move ~/Applications/s3g No Input Mixer.app to Trash.
3. Optional: reset saved DSP, MIDI-source, and audio-routing preferences with:

   defaults delete org.s3g.s3g-dsp.no-input-mixer-standalone

4. Optional: remove no-longer-needed app backups from the backup folder above.

Uninstalling the app does not remove s3g-dsp CLAP plugins or user-saved
.nimgesture files.

Troubleshooting:

- If the installer is blocked, repeat the Open Anyway sequence within one
  hour of the attempted launch.
- If a route mode is unavailable, select a Core Audio device with the required
  number of hardware outputs.
- If the status strip shows AUDIO ERROR, select another device and confirm that
  it is connected and not exclusively held by another application.
- For callback or overload diagnosis, choose Show Realtime Diagnostics from
  the application menu. Reset begins a fresh observation, and Copy Realtime
  Diagnostics Report copies the current device and error counters.

Documentation:

https://s3g.github.io/s3g-dsp/no-input-mixer-standalone.html

License:

s3g-dsp is distributed under the BSD 3-Clause License. See LICENSE.txt.
Third-party notices for incorporated code are included in
THIRD_PARTY_NOTICES.md.
EOF

python3 "$package_verifier" "$staging" \
  --release-version "$release_version"

(cd "$package_work_root" && zip -qry "$candidate_zip" "$package_name")
python3 "$package_verifier" "$candidate_zip" \
  --release-version "$release_version"
(cd "$package_work_root" && shasum -a 256 "$package_name.zip" \
  > "$package_name.zip.sha256")

# Publish only after the staging tree and independently extracted ZIP pass.
# A failed build or verification leaves the previous package untouched.
rm -rf "$final_staging" "$zip_path" "$checksum_path"
mv "$staging" "$final_staging"
mv "$candidate_zip" "$zip_path"
mv "$candidate_checksum" "$checksum_path"
echo "$zip_path"
echo "$checksum_path"
