#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src_root="$repo_root/build-clap/plugins"
dist_root="$repo_root/dist"
manifest="$repo_root/scripts/clap-bundles.tsv"
legacy_manifest="$repo_root/scripts/clap-legacy-bundles.tsv"
manifest_checker="$repo_root/scripts/check-clap-bundle-manifest.py"
release_version="${S3G_RELEASE_VERSION:-0.4.0-pre}"
release_date="${S3G_RELEASE_DATE:-$(date +%F)}"
codesign_identity="${S3G_CODESIGN_IDENTITY:--}"
package_name="${1:-s3g-dsp-macos-clap-$release_version}"
staging="$dist_root/$package_name"
zip_path="$dist_root/$package_name.zip"

if [[ ! "$package_name" =~ ^s3g-dsp-macos-clap-[A-Za-z0-9._-]+$ ]]; then
  echo "Unsafe package name: $package_name" >&2
  exit 2
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

python3 "$manifest_checker" \
  --active-manifest "$manifest" \
  --legacy-manifest "$legacy_manifest" \
  --build-root "$src_root"

rm -rf "$staging" "$zip_path"
mkdir -p "$staging"

# The source build may retain internal target/output names. The release package
# deliberately renames only the outer bundle directory to the canonical name;
# CLAP and CFBundle identifiers remain stable for session compatibility.
for ((i=0; i<bundle_count; i++)); do
  source_bundle="$src_root/${relative_paths[$i]}"
  staged_bundle="$staging/${canonical_names[$i]}"
  cp -R "$source_bundle" "$staged_bundle"
  codesign --force --deep --sign "$codesign_identity" "$staged_bundle"
  codesign --verify --deep --strict "$staged_bundle"
done

cp -R "$repo_root/wavetables/vot" "$staging/VOT Wavetables"
cp -R "$repo_root/examples/voicebanks/s3g-demo-synthetic" "$staging/Ambi Vox Demo Voicebank"
cp "$repo_root/LICENSE" "$staging/LICENSE.txt"
cp "$repo_root/THIRD_PARTY_NOTICES.md" "$staging/THIRD_PARTY_NOTICES.md"

mkdir -p "$staging/Installer Data"
cp "$manifest" "$staging/Installer Data/clap-bundles.tsv"
cp "$legacy_manifest" "$staging/Installer Data/clap-legacy-bundles.tsv"
cp "$repo_root/scripts/install-clap-bundles.sh" "$staging/Install s3g-dsp CLAPs.command"
chmod 755 "$staging/Install s3g-dsp CLAPs.command"

cat > "$staging/README.txt" <<EOF
s3g-dsp pre-release macOS CLAP builds for REAPER testing.

Version: $release_version
Release date: $release_date

These binaries are provided for early testing only. Plugin names, parameter
mappings, state compatibility, and the included plugin set may change before a
stable release.

The bundles are ad-hoc signed by default for bundle-integrity verification but
are not Apple notarized.

Recommended installation:

1. Double-click "Install s3g-dsp CLAPs.command".
2. Rescan CLAP plugins in REAPER.

The installer verifies all bundle identities before changing the user plugin
folder, installs the current bundles under their canonical product names, and
moves recognized renamed/retired s3g-dsp aliases to a timestamped backup under:

~/Library/Application Support/s3g-dsp/CLAP Backups/

Stable plugin identifiers do not change, so the filename migration does not
break session identity. Other products, including s3g-rnbo-clap bundles, are
left untouched. To preview the exact changes from Terminal, run:

./Install\ s3g-dsp\ CLAPs.command --dry-run

Manual drag-copying is possible, but it cannot retire older filenames and may
leave duplicate host entries. The included installer is therefore preferred.

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

(cd "$dist_root" && zip -qry "$zip_path" "$package_name")
echo "$zip_path"
