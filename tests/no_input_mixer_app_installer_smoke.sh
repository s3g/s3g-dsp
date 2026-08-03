#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
installer="$repo_root/scripts/install-no-input-mixer-app.sh"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/s3g-nim-app-installer-smoke.XXXXXX")"
trap 'rm -rf "$test_root"' EXIT

app_name="s3g No Input Mixer.app"
bundle_name="s3g No Input Mixer"
bundle_id="org.s3g.s3g-dsp.no-input-mixer-standalone"
executable_name="s3g No Input Mixer"

fixture_source="$test_root/fixture.c"
fixture_arm64="$test_root/fixture-arm64"
fixture_x86_64="$test_root/fixture-x86_64"

cat > "$fixture_source" <<'EOF'
int main(void) { return 0; }
EOF
/usr/bin/clang -arch arm64 "$fixture_source" -o "$fixture_arm64"
/usr/bin/clang -arch x86_64 "$fixture_source" -o "$fixture_x86_64"
[[ "$(/usr/bin/lipo -archs "$fixture_arm64")" == "arm64" ]]
[[ "$(/usr/bin/lipo -archs "$fixture_x86_64")" == "x86_64" ]]

make_app() {
  local path="$1"
  local identifier="${2:-$bundle_id}"
  local display_name="${3:-$bundle_name}"
  local executable="${4:-$executable_name}"
  local package_type="${5:-APPL}"
  local architecture="${6:-arm64}"
  local marker="${7:-fixture}"
  local short_version="${8:-0.6.0}"
  local bundle_version="${9:-$short_version}"
  local binary="$fixture_arm64"

  if [[ "$architecture" == "x86_64" ]]; then
    binary="$fixture_x86_64"
  fi
  mkdir -p "$path/Contents/MacOS" "$path/Contents/Resources"
  cat > "$path/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key>
  <string>$executable</string>
  <key>CFBundleIdentifier</key>
  <string>$identifier</string>
  <key>CFBundleName</key>
  <string>$display_name</string>
  <key>CFBundlePackageType</key>
  <string>$package_type</string>
  <key>CFBundleShortVersionString</key>
  <string>$short_version</string>
  <key>CFBundleVersion</key>
  <string>$bundle_version</string>
</dict>
</plist>
EOF
  cp "$binary" "$path/Contents/MacOS/$executable"
  chmod 755 "$path/Contents/MacOS/$executable"
  printf '%s\n' "$marker" > "$path/Contents/Resources/build-marker.txt"
  /usr/bin/codesign --force --deep --sign - "$path" >/dev/null 2>&1
}

expect_failure() {
  local status=0
  "$@" >/dev/null 2>&1 || status=$?
  if [[ $status -eq 0 ]]; then
    echo "Expected command to fail: $*" >&2
    return 1
  fi
}

assert_no_quarantine() {
  local path="$1"
  local attributes
  attributes="$(/usr/bin/xattr -r "$path")"
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
  if ! actual="$(/usr/bin/xattr -p "$attribute" "$path" 2>/dev/null)"; then
    echo "Missing $attribute on: $path" >&2
    return 1
  fi
  if [[ "$actual" != "$expected" ]]; then
    echo "Unexpected $attribute on: $path" >&2
    return 1
  fi
}

# Exercise the exact release layout: the renamed .command and app sit together,
# and default installation resolves beneath the test user's ~/Applications.
package_root="$test_root/s3g-no-input-mixer-app-macos-arm64-0.6.0-pre"
package_home="$test_root/package-home"
package_app="$package_root/$app_name"
package_command="$package_root/Install s3g No Input Mixer.command"
package_destination="$package_home/Applications/$app_name"
package_backup_root="$package_home/Library/Application Support/s3g-dsp/No Input Mixer Backups"
mkdir -p "$package_root" "$package_home"
cp "$installer" "$package_command"
chmod 755 "$package_command"
make_app "$package_app" "$bundle_id" "$bundle_name" \
  "$executable_name" APPL arm64 first

# A downloaded archive may quarantine the outer app and nested executable.
# Only the installed copy loses that attribute; unrelated metadata survives.
quarantine_value='0083;fixture;Safari;'
/usr/bin/xattr -w com.apple.quarantine "$quarantine_value" "$package_app"
/usr/bin/xattr -w com.apple.quarantine "$quarantine_value" \
  "$package_app/Contents/MacOS/$executable_name"
/usr/bin/xattr -w org.s3g.installer-fixture preserve-me \
  "$package_app/Contents/Info.plist"

HOME="$package_home" S3G_NIM_APP_TEST_RUNNING=0 \
  "$package_command" --dry-run >/dev/null
[[ ! -e "$package_destination" ]]
[[ ! -e "$package_backup_root" ]]

HOME="$package_home" S3G_NIM_APP_TEST_RUNNING=0 \
  "$package_command" >/dev/null
[[ -d "$package_destination" && ! -L "$package_destination" ]]
[[ "$(cat "$package_destination/Contents/Resources/build-marker.txt")" == "first" ]]
[[ "$(/usr/bin/lipo -archs "$package_destination/Contents/MacOS/$executable_name")" == "arm64" ]]
/usr/bin/codesign --verify --deep --strict "$package_destination"
assert_no_quarantine "$package_destination"
assert_xattr_value org.s3g.installer-fixture preserve-me \
  "$package_destination/Contents/Info.plist"
assert_xattr_value com.apple.quarantine "$quarantine_value" "$package_app"
assert_xattr_value com.apple.quarantine "$quarantine_value" \
  "$package_app/Contents/MacOS/$executable_name"
[[ ! -e "$package_backup_root" ]]

# A valid upgrade is installed only after the old app is moved to a unique,
# identity-verified backup. User-level state is outside the app and untouched.
upgrade_source="$test_root/upgrade-source/$app_name"
make_app "$upgrade_source" "$bundle_id" "$bundle_name" \
  "$executable_name" APPL arm64 second
S3G_NIM_APP_SOURCE="$upgrade_source" \
S3G_NIM_APP_DESTINATION="$package_destination" \
S3G_NIM_APP_BACKUP_ROOT="$package_backup_root" \
S3G_NIM_APP_TEST_RUNNING=0 \
  "$installer" >/dev/null
[[ "$(cat "$package_destination/Contents/Resources/build-marker.txt")" == "second" ]]
backup_markers=("$package_backup_root"/*/"$app_name"/Contents/Resources/build-marker.txt)
[[ ${#backup_markers[@]} -eq 1 ]]
[[ "$(cat "${backup_markers[0]}")" == "first" ]]
/usr/bin/codesign --verify --deep --strict \
  "$(dirname "$(dirname "$(dirname "${backup_markers[0]}")")")"

# A conflicting destination is never moved or overwritten.
conflict_destination="$test_root/conflict/Applications/$app_name"
conflict_backup="$test_root/conflict-backups"
make_app "$conflict_destination" org.example.unrelated "$bundle_name" \
  "$executable_name" APPL arm64 conflict
expect_failure env \
  "S3G_NIM_APP_SOURCE=$upgrade_source" \
  "S3G_NIM_APP_DESTINATION=$conflict_destination" \
  "S3G_NIM_APP_BACKUP_ROOT=$conflict_backup" \
  S3G_NIM_APP_TEST_RUNNING=0 \
  "$installer"
[[ "$(cat "$conflict_destination/Contents/Resources/build-marker.txt")" == "conflict" ]]
[[ ! -e "$conflict_backup" ]]

# A symlink at the exact install path cannot redirect replacement elsewhere.
symlink_parent="$test_root/symlink/Applications"
symlink_target="$test_root/symlink-target/$app_name"
symlink_destination="$symlink_parent/$app_name"
mkdir -p "$symlink_parent"
make_app "$symlink_target" "$bundle_id" "$bundle_name" \
  "$executable_name" APPL arm64 symlink-target
ln -s "$symlink_target" "$symlink_destination"
expect_failure env \
  "S3G_NIM_APP_SOURCE=$upgrade_source" \
  "S3G_NIM_APP_DESTINATION=$symlink_destination" \
  "S3G_NIM_APP_BACKUP_ROOT=$test_root/symlink-backups" \
  S3G_NIM_APP_TEST_RUNNING=0 \
  "$installer"
[[ "$(cat "$symlink_target/Contents/Resources/build-marker.txt")" == "symlink-target" ]]
[[ -L "$symlink_destination" ]]

# The normal running-process gate is deterministic in the smoke test and does
# not create an install folder before it refuses the operation.
running_destination="$test_root/running/Applications/$app_name"
expect_failure env \
  "S3G_NIM_APP_SOURCE=$upgrade_source" \
  "S3G_NIM_APP_DESTINATION=$running_destination" \
  "S3G_NIM_APP_BACKUP_ROOT=$test_root/running-backups" \
  S3G_NIM_APP_TEST_RUNNING=1 \
  "$installer"
[[ ! -e "$running_destination" ]]

# An injected failure after backup must restore the exact previous app rather
# than leave the destination empty or expose the staged upgrade.
rollback_destination="$test_root/rollback/Applications/$app_name"
rollback_backup="$test_root/rollback-backups"
rollback_first="$test_root/rollback-first/$app_name"
make_app "$rollback_first" "$bundle_id" "$bundle_name" \
  "$executable_name" APPL arm64 rollback-first
S3G_NIM_APP_SOURCE="$rollback_first" \
S3G_NIM_APP_DESTINATION="$rollback_destination" \
S3G_NIM_APP_BACKUP_ROOT="$rollback_backup" \
S3G_NIM_APP_TEST_RUNNING=0 \
  "$installer" >/dev/null
expect_failure env \
  "S3G_NIM_APP_SOURCE=$upgrade_source" \
  "S3G_NIM_APP_DESTINATION=$rollback_destination" \
  "S3G_NIM_APP_BACKUP_ROOT=$rollback_backup" \
  S3G_NIM_APP_TEST_RUNNING=0 \
  S3G_NIM_APP_TEST_FAIL_AFTER_BACKUP=1 \
  "$installer"
[[ "$(cat "$rollback_destination/Contents/Resources/build-marker.txt")" == "rollback-first" ]]
/usr/bin/codesign --verify --deep --strict "$rollback_destination"
[[ -z "$(find "$rollback_backup" -name build-marker.txt -print -quit 2>/dev/null)" ]]

# Every identity field, numeric version pair, architecture, and signature is a
# source preflight gate. None of these failures may create a destination.
invalid_root="$test_root/invalid"
invalid_destination="$test_root/invalid-destination/Applications/$app_name"
mkdir -p "$invalid_root"

wrong_id="$invalid_root/wrong-id/$app_name"
make_app "$wrong_id" org.example.wrong "$bundle_name" \
  "$executable_name" APPL arm64 wrong-id
wrong_name="$invalid_root/wrong-name/$app_name"
make_app "$wrong_name" "$bundle_id" "Wrong Name" \
  "$executable_name" APPL arm64 wrong-name
wrong_executable="$invalid_root/wrong-executable/$app_name"
make_app "$wrong_executable" "$bundle_id" "$bundle_name" \
  "Wrong Executable" APPL arm64 wrong-executable
wrong_type="$invalid_root/wrong-type/$app_name"
make_app "$wrong_type" "$bundle_id" "$bundle_name" \
  "$executable_name" BNDL arm64 wrong-type
wrong_arch="$invalid_root/wrong-arch/$app_name"
make_app "$wrong_arch" "$bundle_id" "$bundle_name" \
  "$executable_name" APPL x86_64 wrong-arch
wrong_version="$invalid_root/wrong-version/$app_name"
make_app "$wrong_version" "$bundle_id" "$bundle_name" \
  "$executable_name" APPL arm64 wrong-version 0.6.0 0.6.1
non_numeric_version="$invalid_root/non-numeric-version/$app_name"
make_app "$non_numeric_version" "$bundle_id" "$bundle_name" \
  "$executable_name" APPL arm64 non-numeric 0.6.0-pre 0.6.0-pre
bad_signature="$invalid_root/bad-signature/$app_name"
make_app "$bad_signature" "$bundle_id" "$bundle_name" \
  "$executable_name" APPL arm64 signed-before-change
printf '%s\n' changed-after-signing > \
  "$bad_signature/Contents/Resources/build-marker.txt"

for invalid_source in \
  "$wrong_id" \
  "$wrong_name" \
  "$wrong_executable" \
  "$wrong_type" \
  "$wrong_arch" \
  "$wrong_version" \
  "$non_numeric_version" \
  "$bad_signature"; do
  expect_failure env \
    "S3G_NIM_APP_SOURCE=$invalid_source" \
    "S3G_NIM_APP_DESTINATION=$invalid_destination" \
    "S3G_NIM_APP_BACKUP_ROOT=$test_root/invalid-backups" \
    S3G_NIM_APP_TEST_RUNNING=0 \
    "$installer"
  [[ ! -e "$invalid_destination" ]]
done

echo "No Input Mixer app installer smoke passed"
