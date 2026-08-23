#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir_input="${S3G_CLAP_BUILD_DIR:-$repo_root/build-clap-release}"
dist_root="$repo_root/dist"
manifest="$repo_root/scripts/clap-bundles.tsv"
legacy_manifest="$repo_root/scripts/clap-legacy-bundles.tsv"
manifest_checker="$repo_root/scripts/check-clap-bundle-manifest.py"
objc_symbol_checker="$repo_root/scripts/check-clap-objc-symbols.py"
package_verifier="$repo_root/scripts/verify-macos-clap-package.py"
release_version="${S3G_RELEASE_VERSION:-0.8.0-pre}"
release_date="${S3G_RELEASE_DATE:-$(date +%F)}"
codesign_identity="${S3G_CODESIGN_IDENTITY:--}"
allow_dirty="${S3G_PACKAGE_ALLOW_DIRTY:-0}"
package_name="${1:-s3g-dsp-macos-clap-$release_version}"
final_staging="$dist_root/$package_name"
zip_path="$dist_root/$package_name.zip"
checksum_path="$zip_path.sha256"
expected_bundle_count=128
expected_descriptor_count=132

codesign_args=(--force --deep --sign "$codesign_identity")
if [[ "$codesign_identity" != "-" ]]; then
  codesign_args+=(--options runtime --timestamp)
fi

if [[ ! "$package_name" =~ ^s3g-dsp-macos-clap-[A-Za-z0-9._-]+$ ]]; then
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

if [[ ! -d "$build_dir_input" ]]; then
  echo "Missing CLAP build directory: $build_dir_input" >&2
  echo "Configure the Release tree first (normally build-clap-release)." >&2
  exit 1
fi
build_dir="$(cd "$build_dir_input" && pwd -P)"
src_root="$build_dir/plugins"
cache="$build_dir/CMakeCache.txt"
if [[ ! -f "$cache" ]]; then
  echo "Missing CMake cache for CLAP release build: $cache" >&2
  exit 1
fi
cache_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache")"
cache_type="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$cache")"
cache_configs="$(sed -n 's/^CMAKE_CONFIGURATION_TYPES:STRING=//p' "$cache")"
if [[ -z "$cache_source" || ! -d "$cache_source" ]]; then
  echo "CMake cache has no valid source directory: $cache" >&2
  exit 1
fi
cache_source="$(cd "$cache_source" && pwd -P)"
if [[ "$cache_source" != "$repo_root" ]]; then
  echo "Release artifacts were configured from a different source tree:" >&2
  echo "  expected: $repo_root" >&2
  echo "  cache:    $cache_source" >&2
  exit 1
fi
if [[ "$cache_type" != "Release" && ";$cache_configs;" != *";Release;"* ]]; then
  echo "CLAP package artifacts must come from a Release configuration: $cache" >&2
  echo "  CMAKE_BUILD_TYPE=${cache_type:-<unset>}" >&2
  echo "  CMAKE_CONFIGURATION_TYPES=${cache_configs:-<unset>}" >&2
  exit 1
fi
if [[ ! -d "$src_root" || -L "$src_root" ]]; then
  echo "Missing or unsafe Release plugin artifact root: $src_root" >&2
  exit 1
fi

if [[ "$allow_dirty" -eq 0 && -n "$(git -C "$repo_root" status --porcelain --untracked-files=all)" ]]; then
  echo "Refusing to package a dirty source tree." >&2
  echo "Commit/review the release inputs, or set S3G_PACKAGE_ALLOW_DIRTY=1 for an explicitly non-final test package." >&2
  exit 1
fi

relative_paths=()
canonical_names=()
bundle_ids=()
host_names=()

while IFS=$'\t' read -r relative_path canonical_name bundle_id host_name extra; do
  if [[ -z "${relative_path:-}" || "${relative_path:0:1}" == "#" ]]; then
    continue
  fi
  if [[ -n "${extra:-}" || -z "${host_name:-}" ]]; then
    echo "Malformed active manifest row: $relative_path" >&2
    exit 1
  fi
  relative_paths+=("$relative_path")
  canonical_names+=("$canonical_name")
  bundle_ids+=("$bundle_id")
  host_names+=("$host_name")
done < "$manifest"

bundle_count="${#canonical_names[@]}"
if [[ "$bundle_count" -eq 0 ]]; then
  echo "The CLAP bundle manifest is empty: $manifest" >&2
  exit 1
fi
if [[ "$bundle_count" -ne "$expected_bundle_count" ]]; then
  echo "Release manifest contains $bundle_count bundles; expected $expected_bundle_count" >&2
  echo "Review the release inventory and update the explicit release gate deliberately." >&2
  exit 1
fi

echo "Building exact Release artifact tree: $build_dir"
cmake --build "$build_dir" --config Release --parallel
cache_project_version="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$cache")"
if [[ -z "$cache_project_version" ]]; then
  echo "Release CMake cache has no project version: $cache" >&2
  exit 1
fi
if [[ "$release_version" != "$cache_project_version" \
    && "$release_version" != "$cache_project_version-"* ]]; then
  echo "Suite package version does not match the rebuilt CMake project version:" >&2
  echo "  package: $release_version" >&2
  echo "  project: $cache_project_version" >&2
  exit 1
fi

python3 "$manifest_checker" \
  --active-manifest "$manifest" \
  --legacy-manifest "$legacy_manifest" \
  --build-root "$src_root" \
  --skip-descriptor-check

python3 "$objc_symbol_checker" \
  --manifest "$manifest" \
  --build-root "$src_root"

mkdir -p "$dist_root"
package_work_root="$(mktemp -d "$dist_root/.s3g-clap-package.XXXXXX")"
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
cp "$manifest" "$staging/Installer Data/clap-bundles.tsv"
cp "$legacy_manifest" "$staging/Installer Data/clap-legacy-bundles.tsv"

# The source build may retain internal target/output names. The release package
# deliberately renames only the outer bundle directory to the canonical name;
# CLAP and CFBundle identifiers remain stable for session compatibility.
for ((i=0; i<bundle_count; i++)); do
  source_bundle="$src_root/${relative_paths[$i]}"
  staged_bundle="$staging/${canonical_names[$i]}"
  if [[ ! -d "$source_bundle" || -L "$source_bundle" ]]; then
    echo "Missing or unsafe Release source bundle: $source_bundle" >&2
    exit 1
  fi
  source_plist="$source_bundle/Contents/Info.plist"
  if [[ ! -f "$source_plist" || -L "$source_plist" ]]; then
    echo "Missing or unsafe Release bundle plist: $source_plist" >&2
    exit 1
  fi
  executable_name="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' \
    "$source_plist")"
  executable_path="$source_bundle/Contents/MacOS/$executable_name"
  if [[ ! -f "$executable_path" || -L "$executable_path" || ! -x "$executable_path" ]]; then
    echo "Missing or unsafe Release bundle executable: $executable_path" >&2
    exit 1
  fi
  architectures="$(/usr/bin/lipo -archs "$executable_path")"
  if [[ "$architectures" != "arm64" ]]; then
    echo "Release bundles must be Apple-silicon-only arm64 binaries: $executable_path ($architectures)" >&2
    exit 1
  fi
  cp -R "$source_bundle" "$staged_bundle"
done

# Component descriptors intentionally carry independent versions. Synchronize
# the unsigned staged plist metadata from the actual runtime descriptors before
# signing; the Release executables remain byte-for-byte build artifacts.
python3 "$package_verifier" "$staging" \
  --fix-bundle-metadata \
  --expected-count "$expected_bundle_count" \
  --expected-descriptor-count "$expected_descriptor_count" \
  --reference-manifest "$manifest" \
  --reference-legacy-manifest "$legacy_manifest"

for ((i=0; i<bundle_count; i++)); do
  staged_bundle="$staging/${canonical_names[$i]}"
  codesign "${codesign_args[@]}" "$staged_bundle"
  codesign --verify --deep --strict "$staged_bundle"
done

cp -R "$repo_root/wavetables/vot" "$staging/VOT Wavetables"
cp -R "$repo_root/examples/voicebanks/s3g-demo-synthetic" "$staging/Ambi Vox Demo Voicebank"
cp "$repo_root/LICENSE" "$staging/LICENSE.txt"
cp "$repo_root/THIRD_PARTY_NOTICES.md" "$staging/THIRD_PARTY_NOTICES.md"
cp "$repo_root/RELEASE_NOTES.md" "$staging/RELEASE_NOTES.md"

cp "$repo_root/scripts/install-clap-bundles.sh" "$staging/Install s3g-dsp CLAPs.command"
chmod 755 "$staging/Install s3g-dsp CLAPs.command"

git_revision="$(git -C "$repo_root" rev-parse HEAD)"
git_dirty="clean"
if [[ -n "$(git -C "$repo_root" status --porcelain --untracked-files=all)" ]]; then
  git_dirty="dirty (explicitly allowed for non-final testing)"
fi
cat > "$staging/Installer Data/build-provenance.txt" <<EOF
s3g-dsp package version: $release_version
Source revision: $git_revision
Source status: $git_dirty
CMake configuration: Release
CMake project version: $cache_project_version
Artifact tree: $(basename "$build_dir")/plugins
Active manifest bundles: $bundle_count
EOF

cat > "$staging/README.txt" <<EOF
s3g-dsp pre-release macOS CLAP builds for REAPER testing.

Version: $release_version
Release date: $release_date

Compatibility:

- Apple silicon Macs only (arm64: M1, M2, M3, M4, or newer).
- Intel Macs are not supported by this package.
- REAPER with CLAP support.

These binaries are provided for early testing only. Plugin names, parameter
mappings, state compatibility, and the included plugin set may change before a
stable release.

The bundles are ad-hoc signed by default for bundle-integrity verification but
are not Apple notarized.

Recommended installation:

1. Quit REAPER. For this unnotarized pre-release, sign in with a macOS
   administrator account or have administrator credentials available.
2. Double-click "Install s3g-dsp CLAPs.command".
3. If macOS says it cannot open the command because it is from an unidentified
   developer, click OK.
4. Open System Settings > Privacy & Security, scroll to Security, and click
   Open Anyway for "Install s3g-dsp CLAPs.command". Confirm Open and
   authenticate when macOS asks.
5. If macOS asks whether Terminal may access the downloaded package, click
   Allow. Wait for the installer to finish before opening REAPER.
6. Start REAPER and rescan CLAP plugins if they do not appear automatically.

The installer recursively removes only com.apple.quarantine from each verified
staged s3g-dsp bundle before installation. It does not disable Gatekeeper,
change global security settings, use sudo, or alter unrelated CLAP products.

The installer verifies all bundle identities before changing the user plugin
folder, installs the current bundles under their canonical product names, and
moves recognized renamed/retired s3g-dsp aliases to a timestamped backup under:

~/Library/Application Support/s3g-dsp/CLAP Backups/

The default plugin location is:

~/Library/Audio/Plug-Ins/CLAP/s3g-dsp/

On the first upgrade from the earlier flat layout, verified s3g-dsp bundles in
the parent CLAP folder are also moved to the backup after the nested install
completes. Unrelated products and sibling folders remain untouched.

Stable plugin identifiers do not change, so the filename migration does not
break session identity. Other products, including s3g-rnbo-clap bundles, are
left untouched. To preview the exact changes from Terminal, run:

./Install\ s3g-dsp\ CLAPs.command --dry-run

Manual installation:

1. Close REAPER.
2. In Terminal, change to this unzipped package folder and run:

   destination="\$HOME/Library/Audio/Plug-Ins/CLAP/s3g-dsp"
   mkdir -p "\$destination"
   for bundle in ./*.clap; do
     installed="\$destination/\$(basename "\$bundle")"
     ditto "\$bundle" "\$installed"
     xattr -drs com.apple.quarantine "\$installed"
   done

3. Restart REAPER and rescan CLAP plugins if needed.

Manual copying cannot retire older filenames and may leave duplicate host
entries after an upgrade. The included installer is therefore preferred when
migrating an existing collection.

Included plugins ($bundle_count bundles):

EOF

for ((i=0; i<bundle_count; i++)); do
  printf -- '- %s\n' "${host_names[$i]}" >> "$staging/README.txt"
done

cat >> "$staging/README.txt" <<'EOF'

Docs:

https://s3g.github.io/s3g-dsp/

License:

s3g-dsp is distributed under the BSD 3-Clause License. See LICENSE.txt.
Third-party notices, including WORLD speech vocoder attribution for Ambi Vox
Encoder's WORLD WAV and voicebank paths, are included in
THIRD_PARTY_NOTICES.md.

VOT Wavetable Library:

Use the LOAD button in s3g Ambi Encoder VOT to load any WAV file from the
included VOT Wavetables folder. The library contains twenty-eight 4 x 4 banks,
including four vocal-source atlases.

Ambi Vox Demo Voicebank:

Use the LOAD button in the s3g Ambi Encoder Vox PHRASE panel to select the
included Ambi Vox Demo Voicebank folder. It is a small synthetic UTAU-style
test bank with WAV aliases, oto.ini timing, and pronunciation examples. The
same button can load a vocal WAV for WORLD analysis and resynthesis.
EOF

(python3 "$package_verifier" "$staging" \
  --expected-count "$expected_bundle_count" \
  --expected-descriptor-count "$expected_descriptor_count" \
  --release-version "$release_version" \
  --reference-manifest "$manifest" \
  --reference-legacy-manifest "$legacy_manifest")

(cd "$package_work_root" && zip -qry "$candidate_zip" "$package_name")
python3 "$package_verifier" "$candidate_zip" \
  --expected-count "$expected_bundle_count" \
  --expected-descriptor-count "$expected_descriptor_count" \
  --release-version "$release_version" \
  --reference-manifest "$manifest" \
  --reference-legacy-manifest "$legacy_manifest"
(cd "$package_work_root" && shasum -a 256 "$package_name.zip" > "$package_name.zip.sha256")

# Publish only after both the staging tree and independently extracted archive
# pass. A failed build or verification leaves the previous package untouched.
rm -rf "$final_staging" "$zip_path" "$checksum_path"
mv "$staging" "$final_staging"
mv "$candidate_zip" "$zip_path"
mv "$candidate_checksum" "$checksum_path"
echo "$zip_path"
echo "$checksum_path"
