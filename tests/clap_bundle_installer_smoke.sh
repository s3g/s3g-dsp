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

assert_no_quarantine() {
  local path="$1"
  local attributes
  attributes="$(xattr -r "$path")"
  if [[ "$attributes" == *com.apple.quarantine* ]]; then
    echo "Quarantine remains under: $path" >&2
    return 1
  fi
}

assert_xattr_value() {
  local attribute="$1"
  local expected="$2"
  local path="$3"
  local actual
  if ! actual="$(xattr -p "$attribute" "$path" 2>/dev/null)"; then
    echo "Missing $attribute on: $path" >&2
    return 1
  fi
  if [[ "$actual" != "$expected" ]]; then
    echo "Unexpected $attribute on: $path" >&2
    return 1
  fi
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
custom_default_root="$custom_home/Library/Audio/Plug-Ins/CLAP"
custom_default_receipt="$custom_home/Library/Application Support/s3g-dsp/clap-install-receipt.tsv"
mkdir -p "$custom_home" "$custom_destination"
cp -R "$source_root/clap_alpha/s3g_old_alpha.clap" \
  "$custom_destination/s3g_old_alpha.clap"
make_bundle "$custom_default_root/s3g_family_beta.clap" \
  org.s3g.s3g-dsp.fixture-beta "default-root fixture"
mkdir -p "$(dirname "$custom_default_receipt")"
cat > "$custom_default_receipt" <<'EOF'
# Canonical installed filename	CLAP/CFBundle identifier
s3g_family_beta.clap	org.s3g.s3g-dsp.fixture-beta
EOF
HOME="$custom_home" \
S3G_CLAP_MANIFEST="$manifest" \
S3G_CLAP_LEGACY_MANIFEST="$legacy_manifest" \
S3G_CLAP_BACKUP_ROOT="$test_root/custom-backups" \
  "$installer" --canonicalize-only --destination "$custom_destination" >/dev/null
[[ -d "$custom_destination/s3g_family_alpha.clap" ]]
[[ -d "$custom_default_root/s3g_family_beta.clap" ]]
[[ "$(grep -c '^s3g_family_beta\.clap' "$custom_default_receipt")" == "1" ]]
[[ ! -e "$custom_default_root/s3g-dsp" ]]

# The dedicated collection destination must never be a symlink.
symlink_home="$test_root/symlink-home"
symlink_clap_root="$symlink_home/Library/Audio/Plug-Ins/CLAP"
symlink_target="$test_root/symlink-target"
mkdir -p "$symlink_clap_root" "$symlink_target"
ln -s "$symlink_target" "$symlink_clap_root/s3g-dsp"
status=0
HOME="$symlink_home" \
S3G_CLAP_MANIFEST="$manifest" \
S3G_CLAP_LEGACY_MANIFEST="$legacy_manifest" \
  "$installer" --dry-run >/dev/null 2>&1 || status=$?
[[ "$status" == "1" ]]
[[ -z "$(find "$symlink_target" -mindepth 1 -print -quit)" ]]

# A default canonicalize dry run models the first planned move so a second
# top-level alias targeting the same nested name is backed up, not also moved.
dry_home="$test_root/default-dry-home"
dry_clap_root="$dry_home/Library/Audio/Plug-Ins/CLAP"
dry_destination="$dry_clap_root/s3g-dsp"
make_bundle "$dry_clap_root/s3g_family_alpha.clap" \
  org.s3g.s3g-dsp.fixture-alpha "s3g Family Alpha"
make_bundle "$dry_clap_root/s3g_old_alpha.clap" \
  org.s3g.s3g-dsp.fixture-alpha "s3g Family Alpha"
dry_output="$(HOME="$dry_home" \
  S3G_CLAP_MANIFEST="$manifest" \
  S3G_CLAP_LEGACY_MANIFEST="$legacy_manifest" \
  S3G_CLAP_BACKUP_ROOT="$test_root/default-dry-backups" \
  "$installer" --canonicalize-only --dry-run \
  --destination "$dry_destination/")"
[[ "$(printf '%s\n' "$dry_output" \
  | grep -c "^RENAME   .* -> $dry_destination/s3g_family_alpha\.clap$")" == "1" ]]
[[ "$(printf '%s\n' "$dry_output" \
  | grep -c '^BACK UP  .*s3g_old_alpha\.clap')" == "1" ]]
[[ -d "$dry_clap_root/s3g_family_alpha.clap" ]]
[[ -d "$dry_clap_root/s3g_old_alpha.clap" ]]
[[ ! -e "$dry_destination" ]]
[[ ! -e "$test_root/default-dry-backups" ]]
[[ ! -e "$dry_home/Library/Application Support/s3g-dsp/clap-install-receipt.tsv" ]]

# Exercise the layout shipped in the release archive: canonical bundles and
# the .command live together, with both manifests in Installer Data/.
package_root="$test_root/package"
package_home="$test_root/package-home"
package_clap_root="$package_home/Library/Audio/Plug-Ins/CLAP"
package_destination="$package_clap_root/s3g-dsp"
package_receipt="$package_home/Library/Application Support/s3g-dsp/clap-install-receipt.tsv"
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
# approved installer must remove that metadata only from installed products.
quarantine_value='0083;fixture;Safari;'
xattr -w com.apple.quarantine "$quarantine_value" \
  "$package_root/s3g_family_alpha.clap"
xattr -w com.apple.quarantine "$quarantine_value" \
  "$package_root/s3g_family_alpha.clap/Contents/MacOS/fixture"
xattr -w org.s3g.installer-fixture 'preserve-me' \
  "$package_root/s3g_family_alpha.clap/Contents/Info.plist"

# The default install migrates verified earlier top-level s3g-dsp bundles into
# a backup and leaves unrelated top-level products and attributes untouched.
make_bundle "$package_clap_root/s3g_family_alpha.clap" \
  org.s3g.s3g-dsp.fixture-alpha "s3g Family Alpha"
make_bundle "$package_clap_root/s3g_old_beta.clap" \
  org.s3g.s3g-dsp.fixture-beta "s3g Family Beta"
make_bundle "$package_clap_root/s3g_retired_alpha.clap" \
  org.s3g.s3g-dsp.fixture-retired "retired alpha"
make_bundle "$package_clap_root/s3g_receipt_only.clap" \
  org.s3g.s3g-dsp.fixture-receipt "receipt-only fixture"
make_bundle "$package_clap_root/s3g_rnbo_keep.clap" \
  org.s3g.s3g-rnbo-clap.fixture "unrelated fixture"
xattr -w com.apple.quarantine "$quarantine_value" \
  "$package_clap_root/s3g_rnbo_keep.clap"
xattr -w com.apple.quarantine "$quarantine_value" \
  "$package_clap_root/s3g_rnbo_keep.clap/Contents/MacOS/fixture"
mkdir -p "$(dirname "$package_receipt")"
cat > "$package_receipt" <<'EOF'
# Canonical installed filename	CLAP/CFBundle identifier
s3g_receipt_only.clap	org.s3g.s3g-dsp.fixture-receipt
EOF

HOME="$package_home" \
S3G_CLAP_BACKUP_ROOT="$test_root/package-backups" \
  "$package_root/Install s3g-dsp CLAPs.command" >/dev/null
[[ -d "$package_destination/s3g_family_alpha.clap" ]]
[[ -d "$package_destination/s3g_family_beta.clap" ]]
[[ ! -e "$package_clap_root/s3g_family_alpha.clap" ]]
[[ ! -e "$package_clap_root/s3g_old_beta.clap" ]]
[[ ! -e "$package_clap_root/s3g_retired_alpha.clap" ]]
[[ ! -e "$package_clap_root/s3g_receipt_only.clap" ]]
[[ -d "$package_clap_root/s3g_rnbo_keep.clap" ]]
[[ "$(find "$test_root/package-backups" -type d \
  -path '*/Previous CLAP Root/s3g_family_alpha.clap' | wc -l | tr -d ' ')" == "1" ]]
[[ "$(find "$test_root/package-backups" -type d \
  -path '*/Previous CLAP Root/s3g_old_beta.clap' | wc -l | tr -d ' ')" == "1" ]]
[[ "$(find "$test_root/package-backups" -type d \
  -path '*/Previous CLAP Root/s3g_retired_alpha.clap' | wc -l | tr -d ' ')" == "1" ]]
[[ "$(find "$test_root/package-backups" -type d \
  -path '*/Previous CLAP Root/s3g_receipt_only.clap' | wc -l | tr -d ' ')" == "1" ]]
[[ "$(grep -c '^s3g_family_.*\.clap' "$package_receipt")" == "2" ]]
assert_no_quarantine "$package_destination/s3g_family_alpha.clap"
assert_xattr_value org.s3g.installer-fixture 'preserve-me' \
  "$package_destination/s3g_family_alpha.clap/Contents/Info.plist"
assert_xattr_value com.apple.quarantine "$quarantine_value" \
  "$package_root/s3g_family_alpha.clap"
assert_xattr_value com.apple.quarantine "$quarantine_value" \
  "$package_root/s3g_family_alpha.clap/Contents/MacOS/fixture"
assert_xattr_value com.apple.quarantine "$quarantine_value" \
  "$package_clap_root/s3g_rnbo_keep.clap"
assert_xattr_value com.apple.quarantine "$quarantine_value" \
  "$package_clap_root/s3g_rnbo_keep.clap/Contents/MacOS/fixture"

# canonicalize-only can move a verified older top-level build into the default
# subfolder without replacing its binary, and clears quarantine on that bundle.
nested_home="$test_root/nested-canonicalize-home"
nested_clap_root="$nested_home/Library/Audio/Plug-Ins/CLAP"
nested_destination="$nested_clap_root/s3g-dsp"
mkdir -p "$nested_clap_root"
cp -R "$source_root/clap_alpha/s3g_old_alpha.clap" \
  "$nested_clap_root/s3g_old_alpha.clap"
xattr -w com.apple.quarantine "$quarantine_value" \
  "$nested_clap_root/s3g_old_alpha.clap/Contents/MacOS/fixture"
HOME="$nested_home" \
S3G_CLAP_BACKUP_ROOT="$test_root/nested-canonicalize-backups" \
S3G_CLAP_RECEIPT="$test_root/nested-canonicalize-receipt.tsv" \
  "$package_root/Install s3g-dsp CLAPs.command" --canonicalize-only >/dev/null
[[ -d "$nested_destination/s3g_family_alpha.clap" ]]
[[ ! -e "$nested_clap_root/s3g_old_alpha.clap" ]]
assert_no_quarantine "$nested_destination/s3g_family_alpha.clap"

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
