#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
installer="$repo_root/scripts/install-clap-bundles.sh"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/s3g-clap-installer-smoke.XXXXXX")"
trap 'rm -rf "$test_root"' EXIT

manifest="$test_root/clap-bundles.tsv"
legacy_manifest="$test_root/clap-legacy-bundles.tsv"
source_root="$test_root/source"
destination="$test_root/destination"
canonicalize_destination="$test_root/canonicalize-destination"
backup_root="$test_root/backups"
receipt="$test_root/receipt.tsv"

make_bundle() {
  local path="$1"
  local identifier="$2"
  local name="$3"
  mkdir -p "$path/Contents/MacOS"
  cat > "$path/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key>
  <string>fixture</string>
  <key>CFBundleIdentifier</key>
  <string>$identifier</string>
  <key>CFBundleName</key>
  <string>$name</string>
</dict>
</plist>
EOF
  cat > "$path/Contents/MacOS/fixture" <<'EOF'
#!/usr/bin/env sh
exit 0
EOF
  chmod 755 "$path/Contents/MacOS/fixture"
}

cat > "$manifest" <<'EOF'
# Relative build path	Canonical installed filename	CLAP/CFBundle identifier	Current host name
clap_alpha/s3g_old_alpha.clap	s3g_family_alpha.clap	org.s3g.s3g-dsp.fixture-alpha	s3g Family Alpha
clap_beta/s3g_old_beta.clap	s3g_family_beta.clap	org.s3g.s3g-dsp.fixture-beta	s3g Family Beta
EOF

cat > "$legacy_manifest" <<'EOF'
# Retired installed filename	Expected CFBundle identifier	Current replacement
s3g_retired_alpha.clap	org.s3g.s3g-dsp.fixture-retired	retired
EOF

make_bundle "$source_root/clap_alpha/s3g_old_alpha.clap" \
  org.s3g.s3g-dsp.fixture-alpha "s3g Family Alpha"
make_bundle "$source_root/clap_beta/s3g_old_beta.clap" \
  org.s3g.s3g-dsp.fixture-beta "s3g Family Beta"

mkdir -p "$destination"
cp -R "$source_root/clap_alpha/s3g_old_alpha.clap" \
  "$destination/s3g_old_alpha.clap"
make_bundle "$destination/s3g_family_beta.clap" \
  org.s3g.s3g-dsp.fixture-beta "previous beta"
make_bundle "$destination/s3g_retired_alpha.clap" \
  org.s3g.s3g-dsp.fixture-retired "retired alpha"
make_bundle "$destination/s3g_rnbo_keep.clap" \
  org.s3g.s3g-rnbo-clap.fixture "unrelated fixture"

installer_env=(
  "S3G_CLAP_MANIFEST=$manifest"
  "S3G_CLAP_LEGACY_MANIFEST=$legacy_manifest"
  "S3G_CLAP_SOURCE_ROOT=$source_root"
  "S3G_CLAP_SOURCE_LAYOUT=build"
  "S3G_CLAP_BACKUP_ROOT=$backup_root"
  "S3G_CLAP_RECEIPT=$receipt"
)

env "${installer_env[@]}" "$installer" --dry-run --destination "$destination" >/dev/null
[[ ! -e "$destination/s3g_family_alpha.clap" ]]
[[ -d "$destination/s3g_old_alpha.clap" ]]

env "${installer_env[@]}" "$installer" --destination "$destination" >/dev/null
[[ -d "$destination/s3g_family_alpha.clap" ]]
[[ -d "$destination/s3g_family_beta.clap" ]]
[[ ! -e "$destination/s3g_old_alpha.clap" ]]
[[ ! -e "$destination/s3g_retired_alpha.clap" ]]
[[ -d "$destination/s3g_rnbo_keep.clap" ]]
[[ "$(find "$backup_root" -type d -name s3g_old_alpha.clap | wc -l | tr -d ' ')" == "1" ]]
[[ "$(find "$backup_root" -type d -name s3g_retired_alpha.clap | wc -l | tr -d ' ')" == "1" ]]
[[ "$(grep -c '^s3g_family_.*\.clap' "$receipt")" == "2" ]]

mkdir -p "$canonicalize_destination"
cp -R "$source_root/clap_alpha/s3g_old_alpha.clap" \
  "$canonicalize_destination/s3g_old_alpha.clap"
cp -R "$source_root/clap_beta/s3g_old_beta.clap" \
  "$canonicalize_destination/s3g_old_beta.clap"
make_bundle "$canonicalize_destination/s3g_family_beta.clap" \
  org.s3g.s3g-dsp.fixture-beta "existing canonical beta"
make_bundle "$canonicalize_destination/s3g_rnbo_keep.clap" \
  org.s3g.s3g-rnbo-clap.fixture "unrelated fixture"

canonical_receipt="$test_root/canonicalize-receipt.tsv"
canonical_backup="$test_root/canonicalize-backups"
canonical_env=(
  "S3G_CLAP_MANIFEST=$manifest"
  "S3G_CLAP_LEGACY_MANIFEST=$legacy_manifest"
  "S3G_CLAP_BACKUP_ROOT=$canonical_backup"
  "S3G_CLAP_RECEIPT=$canonical_receipt"
)

canonical_dry_run="$(env "${canonical_env[@]}" "$installer" \
  --canonicalize-only --dry-run --destination "$canonicalize_destination")"
[[ "$(printf '%s\n' "$canonical_dry_run" \
  | grep -c '^BACK UP  s3g_old_beta\.clap')" == "1" ]]

env "${canonical_env[@]}" "$installer" --canonicalize-only \
  --destination "$canonicalize_destination" >/dev/null
[[ -d "$canonicalize_destination/s3g_family_alpha.clap" ]]
[[ -d "$canonicalize_destination/s3g_family_beta.clap" ]]
[[ ! -e "$canonicalize_destination/s3g_old_alpha.clap" ]]
[[ ! -e "$canonicalize_destination/s3g_old_beta.clap" ]]
[[ -d "$canonicalize_destination/s3g_rnbo_keep.clap" ]]
[[ "$(find "$canonical_backup" -type d -name s3g_old_beta.clap | wc -l | tr -d ' ')" == "1" ]]

make_bundle "$canonicalize_destination/s3g_retired_alpha.clap" \
  org.example.not-owned "identity mismatch"
status=0
env "${canonical_env[@]}" "$installer" --canonicalize-only \
  --destination "$canonicalize_destination" >/dev/null 2>&1 || status=$?
[[ "$status" == "3" ]]
[[ -d "$canonicalize_destination/s3g_retired_alpha.clap" ]]

# A custom destination must not read or overwrite the default user receipt.
custom_home="$test_root/custom-home"
custom_destination="$test_root/custom-no-receipt-destination"
mkdir -p "$custom_home" "$custom_destination"
cp -R "$source_root/clap_alpha/s3g_old_alpha.clap" \
  "$custom_destination/s3g_old_alpha.clap"
HOME="$custom_home" \
S3G_CLAP_MANIFEST="$manifest" \
S3G_CLAP_LEGACY_MANIFEST="$legacy_manifest" \
  "$installer" --canonicalize-only --destination "$custom_destination" >/dev/null
[[ -d "$custom_destination/s3g_family_alpha.clap" ]]
[[ ! -e "$custom_home/Library/Application Support/s3g-dsp/clap-install-receipt.tsv" ]]

# Exercise the layout shipped in the release archive: canonical bundles and
# the .command live together, with both manifests in Installer Data/.
package_root="$test_root/package"
package_destination="$test_root/package-destination"
mkdir -p "$package_root/Installer Data"
cp "$installer" "$package_root/Install s3g-dsp CLAPs.command"
chmod 755 "$package_root/Install s3g-dsp CLAPs.command"
cp "$manifest" "$package_root/Installer Data/clap-bundles.tsv"
cp "$legacy_manifest" "$package_root/Installer Data/clap-legacy-bundles.tsv"
cp -R "$source_root/clap_alpha/s3g_old_alpha.clap" \
  "$package_root/s3g_family_alpha.clap"
cp -R "$source_root/clap_beta/s3g_old_beta.clap" \
  "$package_root/s3g_family_beta.clap"

# A downloaded archive can quarantine both a bundle and its executable. The
# approved installer must not propagate that metadata to installed products.
xattr -w com.apple.quarantine '0083;fixture;Safari;' \
  "$package_root/s3g_family_alpha.clap"
xattr -w com.apple.quarantine '0083;fixture;Safari;' \
  "$package_root/s3g_family_alpha.clap/Contents/MacOS/fixture"

S3G_CLAP_BACKUP_ROOT="$test_root/package-backups" \
S3G_CLAP_RECEIPT="$test_root/package-receipt.tsv" \
  "$package_root/Install s3g-dsp CLAPs.command" \
  --destination "$package_destination" >/dev/null
[[ -d "$package_destination/s3g_family_alpha.clap" ]]
[[ -d "$package_destination/s3g_family_beta.clap" ]]
[[ "$(grep -c '^s3g_family_.*\.clap' "$test_root/package-receipt.tsv")" == "2" ]]
! xattr -p com.apple.quarantine \
  "$package_destination/s3g_family_alpha.clap" >/dev/null 2>&1
! xattr -p com.apple.quarantine \
  "$package_destination/s3g_family_alpha.clap/Contents/MacOS/fixture" \
  >/dev/null 2>&1

# Direct build-layout installs reject stale host metadata and non-executable
# binaries before touching the destination.
/usr/libexec/PlistBuddy -c 'Set :CFBundleName stale name' \
  "$source_root/clap_alpha/s3g_old_alpha.clap/Contents/Info.plist"
status=0
env "${installer_env[@]}" "$installer" --dry-run \
  --destination "$test_root/invalid-host-destination" >/dev/null 2>&1 || status=$?
[[ "$status" == "1" ]]
/usr/libexec/PlistBuddy -c 'Set :CFBundleName s3g Family Alpha' \
  "$source_root/clap_alpha/s3g_old_alpha.clap/Contents/Info.plist"

chmod 644 "$source_root/clap_alpha/s3g_old_alpha.clap/Contents/MacOS/fixture"
status=0
env "${installer_env[@]}" "$installer" --dry-run \
  --destination "$test_root/non-executable-destination" >/dev/null 2>&1 || status=$?
[[ "$status" == "1" ]]

echo "CLAP bundle installer smoke passed"
