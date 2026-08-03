#!/usr/bin/env bash
set -euo pipefail

# Install the standalone No Input Mixer application for the current user. The
# application is deliberately kept separate from the CLAP collection: it has a
# stable bundle identity, its own upgrade backups, and no system-wide writes.

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

expected_app_name="s3g No Input Mixer.app"
expected_bundle_name="s3g No Input Mixer"
expected_bundle_id="org.s3g.s3g-dsp.no-input-mixer-standalone"
expected_executable="s3g No Input Mixer"
expected_package_type="APPL"

source_app="${S3G_NIM_APP_SOURCE:-$script_dir/$expected_app_name}"
destination="${S3G_NIM_APP_DESTINATION:-$HOME/Applications/$expected_app_name}"
backup_root="${S3G_NIM_APP_BACKUP_ROOT:-$HOME/Library/Application Support/s3g-dsp/No Input Mixer Backups}"

# Test-only controls provide deterministic process and rollback coverage
# without weakening normal defaults. Setting TEST_RUNNING=0 bypasses process
# enumeration; 1 simulates a running app. FAIL_AFTER_BACKUP exercises rollback.
test_running="${S3G_NIM_APP_TEST_RUNNING:-auto}"
test_fail_after_backup="${S3G_NIM_APP_TEST_FAIL_AFTER_BACKUP:-0}"

dry_run=0

usage() {
  cat <<'EOF'
Usage: install-no-input-mixer-app.sh [options]

Options:
  --destination PATH  Install to the full PATH ending in
                      "s3g No Input Mixer.app". The default is:
                      ~/Applications/s3g No Input Mixer.app
  --dry-run           Validate and show the planned changes without writing.
  --help              Show this help.

On upgrade, an identity-verified previous application is retained under:
  ~/Library/Application Support/s3g-dsp/No Input Mixer Backups/

The installer writes only to the current user's folders and must not be run
with sudo. Environment overrides are available for package verification:
  S3G_NIM_APP_SOURCE, S3G_NIM_APP_DESTINATION,
  S3G_NIM_APP_BACKUP_ROOT
EOF
}

normalize_install_path() {
  local value="$1"
  while [[ "$value" != "/" && ( "$value" == */ || "$value" == */. ) ]]; do
    value="${value%/}"
    value="${value%/.}"
  done
  printf '%s\n' "$value"
}

fail() {
  echo "$*" >&2
  exit 1
}

usage_error() {
  echo "$*" >&2
  usage >&2
  exit 2
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --destination)
      if [[ $# -lt 2 ]]; then
        usage_error "--destination requires a path"
      fi
      destination="$2"
      shift 2
      ;;
    --dry-run)
      dry_run=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -*)
      usage_error "Unknown option: $1"
      ;;
    *)
      usage_error "Unexpected argument: $1"
      ;;
  esac
done

if [[ $# -gt 0 ]]; then
  usage_error "Unexpected argument: $1"
fi

if [[ ${EUID:-$(id -u)} -eq 0 ]]; then
  fail "Do not run this installer with sudo or as root. It installs only for the current user."
fi

case "$test_running" in
  auto|0|1) ;;
  *) usage_error "S3G_NIM_APP_TEST_RUNNING must be auto, 0, or 1" ;;
esac
case "$test_fail_after_backup" in
  0|1) ;;
  *) usage_error "S3G_NIM_APP_TEST_FAIL_AFTER_BACKUP must be 0 or 1" ;;
esac

source_app="$(normalize_install_path "$source_app")"
destination="$(normalize_install_path "$destination")"
backup_root="$(normalize_install_path "$backup_root")"
destination_parent="$(dirname "$destination")"

if [[ "$source_app" != /* ]]; then
  usage_error "S3G_NIM_APP_SOURCE must be an absolute path: $source_app"
fi
if [[ "$destination" != /* ]]; then
  usage_error "The application destination must be an absolute path: $destination"
fi
if [[ "$(basename "$destination")" != "$expected_app_name" ]]; then
  usage_error "The destination must end in '$expected_app_name': $destination"
fi
if [[ "$backup_root" != /* ]]; then
  usage_error "S3G_NIM_APP_BACKUP_ROOT must be an absolute path: $backup_root"
fi
if [[ "$source_app" == "$destination" ]]; then
  fail "The packaged source and installed destination must be different: $destination"
fi
case "$backup_root/" in
  "$destination/"*)
    fail "The backup folder must not be inside the installed application: $backup_root"
    ;;
esac

bundle_plist_value() {
  local app="$1"
  local key="$2"
  /usr/libexec/PlistBuddy -c "Print :$key" \
    "$app/Contents/Info.plist" 2>/dev/null
}

validate_app() {
  local app="$1"
  local context="$2"
  local plist="$app/Contents/Info.plist"
  local bundle_id bundle_name executable package_type short_version
  local bundle_version architectures linked

  if [[ ! -d "$app" || -L "$app" ]]; then
    echo "$context is missing, is not an application bundle, or is a symlink: $app" >&2
    return 1
  fi
  if [[ ! -f "$plist" || -L "$plist" ]]; then
    echo "$context has a missing or unsafe Info.plist: $plist" >&2
    return 1
  fi
  linked="$(/usr/bin/find "$app" -type l -print -quit)"
  if [[ -n "$linked" ]]; then
    echo "$context contains an unexpected symbolic link: $linked" >&2
    return 1
  fi
  if ! bundle_id="$(bundle_plist_value "$app" CFBundleIdentifier)" \
      || [[ "$bundle_id" != "$expected_bundle_id" ]]; then
    echo "$context bundle identity mismatch: $app" >&2
    echo "  expected: $expected_bundle_id" >&2
    echo "  found:    ${bundle_id:-<unreadable>}" >&2
    return 1
  fi
  if ! bundle_name="$(bundle_plist_value "$app" CFBundleName)" \
      || [[ "$bundle_name" != "$expected_bundle_name" ]]; then
    echo "$context bundle name mismatch: $app" >&2
    echo "  expected: $expected_bundle_name" >&2
    echo "  found:    ${bundle_name:-<unreadable>}" >&2
    return 1
  fi
  if ! executable="$(bundle_plist_value "$app" CFBundleExecutable)" \
      || [[ "$executable" != "$expected_executable" ]]; then
    echo "$context executable declaration mismatch: $app" >&2
    echo "  expected: $expected_executable" >&2
    echo "  found:    ${executable:-<unreadable>}" >&2
    return 1
  fi
  if ! package_type="$(bundle_plist_value "$app" CFBundlePackageType)" \
      || [[ "$package_type" != "$expected_package_type" ]]; then
    echo "$context package type mismatch: $app" >&2
    echo "  expected: $expected_package_type" >&2
    echo "  found:    ${package_type:-<unreadable>}" >&2
    return 1
  fi
  if ! short_version="$(bundle_plist_value "$app" CFBundleShortVersionString)" \
      || [[ ! "$short_version" =~ ^[0-9]+([.][0-9]+)*$ ]]; then
    echo "$context has an invalid CFBundleShortVersionString: $app" >&2
    echo "  found: ${short_version:-<unreadable>}" >&2
    return 1
  fi
  if ! bundle_version="$(bundle_plist_value "$app" CFBundleVersion)" \
      || [[ ! "$bundle_version" =~ ^[0-9]+([.][0-9]+)*$ ]]; then
    echo "$context has an invalid CFBundleVersion: $app" >&2
    echo "  found: ${bundle_version:-<unreadable>}" >&2
    return 1
  fi
  if [[ "$short_version" != "$bundle_version" ]]; then
    echo "$context bundle versions do not match: $app" >&2
    echo "  CFBundleShortVersionString: $short_version" >&2
    echo "  CFBundleVersion:            $bundle_version" >&2
    return 1
  fi

  local executable_path="$app/Contents/MacOS/$expected_executable"
  if [[ ! -f "$executable_path" || -L "$executable_path" \
      || ! -x "$executable_path" ]]; then
    echo "$context has a missing or unsafe executable: $executable_path" >&2
    return 1
  fi
  if ! architectures="$(/usr/bin/lipo -archs "$executable_path" 2>/dev/null)" \
      || [[ "$architectures" != "arm64" ]]; then
    echo "$context must contain one arm64 executable: $executable_path" >&2
    echo "  found: ${architectures:-<unreadable>}" >&2
    return 1
  fi
  if ! /usr/bin/codesign --verify --deep --strict "$app" >/dev/null 2>&1; then
    echo "$context has an invalid code signature: $app" >&2
    return 1
  fi
}

validate_destination_parent() {
  if [[ -L "$destination_parent" ]]; then
    fail "Refusing to install through a symlinked Applications folder: $destination_parent"
  fi
  if [[ -e "$destination_parent" && ! -d "$destination_parent" ]]; then
    fail "The application destination parent is not a directory: $destination_parent"
  fi
  if [[ -L "$backup_root" ]]; then
    fail "Refusing to use a symlink as the backup folder: $backup_root"
  fi
  if [[ -e "$backup_root" && ! -d "$backup_root" ]]; then
    fail "The backup location exists but is not a directory: $backup_root"
  fi
}

validate_existing_destination() {
  if [[ ! -e "$destination" && ! -L "$destination" ]]; then
    return 0
  fi
  if [[ -L "$destination" || ! -d "$destination" ]]; then
    fail "Refusing to replace a non-application or symlink: $destination"
  fi
  validate_app "$destination" "Installed application" || \
    fail "The existing destination was left untouched."
}

app_is_running() {
  case "$test_running" in
    0) return 1 ;;
    1) return 0 ;;
  esac
  /usr/bin/pgrep -f "/s3g No Input Mixer[.]app/Contents/MacOS/s3g No Input Mixer" \
    >/dev/null 2>&1
}

validate_app "$source_app" "Packaged application" || exit 1
validate_destination_parent
validate_existing_destination

if app_is_running; then
  fail "s3g No Input Mixer is running. Quit it before installing so unsaved gesture recordings are not lost."
fi

default_user_destination="$HOME/Applications/$expected_app_name"
system_destination="/Applications/$expected_app_name"
if [[ "$destination" == "$default_user_destination" \
    && ( -e "$system_destination" || -L "$system_destination" ) ]]; then
  echo "WARNING: another copy exists at $system_destination" >&2
  echo "The user-level installer will not alter it. Remove that copy manually if macOS launches the older build." >&2
fi

echo "s3g No Input Mixer user application installer"
echo "Source:      $source_app"
echo "Destination: $destination"
echo "Backup root: $backup_root"

if [[ $dry_run -eq 1 ]]; then
  echo "Mode:        dry run"
  if [[ -e "$destination" ]]; then
    echo "BACK UP  $destination -> $backup_root/<timestamp>/$expected_app_name"
  fi
  echo "INSTALL  $source_app -> $destination"
  echo "No files were changed."
  exit 0
fi

echo "Mode:        install"
/bin/mkdir -p "$destination_parent"

stage_root="$(/usr/bin/mktemp -d "$destination_parent/.s3g-nim-install.XXXXXX")"
staged_app="$stage_root/$expected_app_name"
backup_run=""
previous_backup=""
transaction_active=0
installed_candidate=0
transaction_committed=0

cleanup_on_exit() {
  local status=$?
  trap - EXIT
  set +e

  if [[ $status -ne 0 && $transaction_active -eq 1 \
      && $transaction_committed -eq 0 ]]; then
    if [[ $installed_candidate -eq 1 \
        && ( -e "$destination" || -L "$destination" ) ]]; then
      /bin/mv "$destination" "$stage_root/.failed-$expected_app_name"
    fi
    if [[ -n "$previous_backup" && -d "$previous_backup" \
        && ! -e "$destination" && ! -L "$destination" ]]; then
      if /bin/mv "$previous_backup" "$destination"; then
        echo "RESTORED  $destination" >&2
      else
        echo "WARNING: automatic rollback failed; the previous app remains at:" >&2
        echo "  $previous_backup" >&2
      fi
    fi
    if [[ -n "$backup_run" ]]; then
      /bin/rmdir "$backup_run" 2>/dev/null
    fi
  fi

  case "$stage_root" in
    "$destination_parent/.s3g-nim-install."*)
      if [[ -d "$stage_root" ]]; then
        /bin/rm -rf "$stage_root"
      fi
      ;;
    *)
      echo "WARNING: refusing to clean an unexpected staging path: $stage_root" >&2
      ;;
  esac
  exit "$status"
}
trap cleanup_on_exit EXIT
trap 'exit 1' HUP INT TERM

# Copy and completely validate the new app before the existing installation is
# moved. ditto preserves the bundle and signature. Only Gatekeeper quarantine
# is removed, after the user has explicitly approved this installer.
/usr/bin/ditto --noqtn "$source_app" "$staged_app"
/usr/bin/xattr -drs com.apple.quarantine "$staged_app"
validate_app "$staged_app" "Staged application" || exit 1

transaction_active=1
if [[ -e "$destination" ]]; then
  /bin/mkdir -p "$backup_root"
  backup_run="$backup_root/$(/bin/date +%Y%m%d-%H%M%S)-$$"
  if [[ -e "$backup_run" || -L "$backup_run" ]]; then
    fail "Refusing to reuse an existing backup folder: $backup_run"
  fi
  /bin/mkdir "$backup_run"
  previous_backup="$backup_run/$expected_app_name"
  /bin/mv "$destination" "$previous_backup"
  validate_app "$previous_backup" "Backed-up application" || exit 1
  echo "BACKED UP $previous_backup"

  if [[ "$test_fail_after_backup" == "1" ]]; then
    fail "Injected test failure after backup"
  fi
fi

if ! /bin/mv "$staged_app" "$destination"; then
  fail "Failed to install $expected_app_name"
fi
installed_candidate=1
validate_app "$destination" "Installed application" || exit 1
transaction_committed=1

echo "INSTALLED $destination"
if [[ -n "$previous_backup" ]]; then
  echo "Previous version retained at: $previous_backup"
fi
echo "The app was not opened automatically. Confirm its output route before selecting AUDIO ON."
