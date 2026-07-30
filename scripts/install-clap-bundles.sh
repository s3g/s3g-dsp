#!/usr/bin/env bash
set -euo pipefail

# Install the active s3g-dsp CLAP set under the canonical, host-facing bundle
# names recorded in clap-bundles.tsv. Stable CLAP/CFBundle identifiers are
# deliberately not renamed: hosts use those identifiers to recall sessions.

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -f "$script_dir/Installer Data/clap-bundles.tsv" ]]; then
  default_manifest="$script_dir/Installer Data/clap-bundles.tsv"
  default_legacy_manifest="$script_dir/Installer Data/clap-legacy-bundles.tsv"
  default_source_root="$script_dir"
  default_source_layout="flat"
else
  default_manifest="$script_dir/clap-bundles.tsv"
  default_legacy_manifest="$script_dir/clap-legacy-bundles.tsv"
  default_source_root="$(cd "$script_dir/.." && pwd)/build-clap/plugins"
  default_source_layout="build"
fi

manifest="${S3G_CLAP_MANIFEST:-$default_manifest}"
legacy_manifest="${S3G_CLAP_LEGACY_MANIFEST:-$default_legacy_manifest}"
source_root="${S3G_CLAP_SOURCE_ROOT:-$default_source_root}"
source_layout="${S3G_CLAP_SOURCE_LAYOUT:-$default_source_layout}"
default_clap_root="$HOME/Library/Audio/Plug-Ins/CLAP"
default_destination="$default_clap_root/s3g-dsp"
default_receipt="$HOME/Library/Application Support/s3g-dsp/clap-install-receipt.tsv"
destination="${S3G_CLAP_DESTINATION:-$default_destination}"
backup_parent="${S3G_CLAP_BACKUP_ROOT:-$HOME/Library/Application Support/s3g-dsp/CLAP Backups}"
receipt="${S3G_CLAP_RECEIPT:-$default_receipt}"

dry_run=0
canonicalize_only=0
destination_was_set=0

usage() {
  cat <<'EOF'
Usage: install-clap-bundles.sh [options] [destination]

Options:
  --destination PATH   Install directly into PATH instead of the default
                       user CLAP/s3g-dsp folder.
  --dry-run            Validate and show changes without writing anything.
  --canonicalize-only  Rename/retire an existing install without copying builds.
  --help               Show this help.

Renamed and retired bundles are moved to a timestamped backup under:
  ~/Library/Application Support/s3g-dsp/CLAP Backups/

The default install location is:
  ~/Library/Audio/Plug-Ins/CLAP/s3g-dsp/

Environment overrides are available for automation:
  S3G_CLAP_MANIFEST, S3G_CLAP_LEGACY_MANIFEST, S3G_CLAP_SOURCE_ROOT,
  S3G_CLAP_SOURCE_LAYOUT (build|flat), S3G_CLAP_DESTINATION,
  S3G_CLAP_BACKUP_ROOT, and S3G_CLAP_RECEIPT.

The default receipt is used only for the default user CLAP/s3g-dsp folder. Set
S3G_CLAP_RECEIPT explicitly when installing to a custom destination and a
persistent ownership receipt is desired there.
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

while [[ $# -gt 0 ]]; do
  case "$1" in
    --destination)
      if [[ $# -lt 2 ]]; then
        echo "--destination requires a path" >&2
        exit 2
      fi
      destination="$2"
      destination_was_set=1
      shift 2
      ;;
    --dry-run)
      dry_run=1
      shift
      ;;
    --canonicalize-only)
      canonicalize_only=1
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
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      if [[ $destination_was_set -eq 1 ]]; then
        echo "Destination was specified more than once" >&2
        exit 2
      fi
      destination="$1"
      destination_was_set=1
      shift
      ;;
  esac
done

if [[ $# -gt 0 ]]; then
  echo "Unexpected argument: $1" >&2
  exit 2
fi

destination="$(normalize_install_path "$destination")"

if [[ -L "$destination" ]]; then
  echo "Refusing to use a symlink as the CLAP destination: $destination" >&2
  exit 1
fi
if [[ -e "$destination" && ! -d "$destination" ]]; then
  echo "CLAP destination exists but is not a directory: $destination" >&2
  exit 1
fi

# A receipt describes one destination. Never read or overwrite the normal user
# receipt while operating on a custom test, system, or staging folder unless
# the caller supplied an explicit receipt path for that destination.
if [[ "$destination" != "$default_destination" && -z "${S3G_CLAP_RECEIPT+x}" ]]; then
  receipt=""
fi

migrate_previous_default=0
if [[ "$destination" == "$default_destination" ]]; then
  migrate_previous_default=1
fi

if [[ "$source_layout" != "build" && "$source_layout" != "flat" ]]; then
  echo "S3G_CLAP_SOURCE_LAYOUT must be 'build' or 'flat': $source_layout" >&2
  exit 2
fi

if [[ ! -f "$manifest" ]]; then
  echo "Missing active CLAP manifest: $manifest" >&2
  exit 1
fi
if [[ ! -f "$legacy_manifest" ]]; then
  echo "Missing legacy CLAP manifest: $legacy_manifest" >&2
  exit 1
fi

safe_bundle_name() {
  [[ "$1" =~ ^s3g_[a-z0-9_]+\.clap$ ]]
}

safe_relative_bundle_path() {
  [[ "$1" != /* && "$1" != *".."* && "$1" == */*.clap ]]
}

bundle_plist_value() {
  local bundle="$1"
  local key="$2"
  local plist="$bundle/Contents/Info.plist"
  [[ -f "$plist" ]] || return 1
  /usr/libexec/PlistBuddy -c "Print :$key" "$plist" 2>/dev/null
}

bundle_id() {
  bundle_plist_value "$1" CFBundleIdentifier
}

expected_id_matches() {
  local expected="$1"
  local actual="$2"
  case "$expected" in
    *\*)
      [[ "$actual" == "${expected%\*}"* ]]
      ;;
    *)
      [[ "$actual" == "$expected" ]]
      ;;
  esac
}

relative_paths=()
canonical_names=()
bundle_ids=()
host_names=()

while IFS=$'\t' read -r relative_path canonical_name expected_id host_name extra; do
  if [[ -z "${relative_path:-}" || "${relative_path:0:1}" == "#" ]]; then
    continue
  fi
  if [[ -n "${extra:-}" || -z "${host_name:-}" ]]; then
    echo "Malformed active manifest row: $relative_path" >&2
    exit 1
  fi
  if ! safe_relative_bundle_path "$relative_path"; then
    echo "Unsafe build bundle path in manifest: $relative_path" >&2
    exit 1
  fi
  if ! safe_bundle_name "$canonical_name"; then
    echo "Unsafe canonical bundle name in manifest: $canonical_name" >&2
    exit 1
  fi
  if [[ "$expected_id" != org.s3g.s3g-dsp.* ]]; then
    echo "Unexpected active bundle identifier: $expected_id" >&2
    exit 1
  fi
  relative_paths+=("$relative_path")
  canonical_names+=("$canonical_name")
  bundle_ids+=("$expected_id")
  host_names+=("$host_name")
done < "$manifest"

if [[ ${#canonical_names[@]} -eq 0 ]]; then
  echo "The active CLAP manifest is empty: $manifest" >&2
  exit 1
fi

legacy_names=()
legacy_ids=()
legacy_replacements=()

while IFS=$'\t' read -r legacy_name expected_id replacement extra; do
  if [[ -z "${legacy_name:-}" || "${legacy_name:0:1}" == "#" ]]; then
    continue
  fi
  if [[ -n "${extra:-}" || -z "${replacement:-}" ]]; then
    echo "Malformed legacy manifest row: $legacy_name" >&2
    exit 1
  fi
  if ! safe_bundle_name "$legacy_name"; then
    echo "Unsafe legacy bundle name in manifest: $legacy_name" >&2
    exit 1
  fi
  if [[ "$expected_id" != org.s3g.s3g-dsp.* && "$expected_id" != org.s3g.s3g-dsp.*\* ]]; then
    echo "Unexpected legacy bundle identifier pattern: $expected_id" >&2
    exit 1
  fi
  if [[ "$replacement" != "retired" ]] && ! safe_bundle_name "$replacement"; then
    echo "Unsafe legacy replacement in manifest: $replacement" >&2
    exit 1
  fi
  legacy_names+=("$legacy_name")
  legacy_ids+=("$expected_id")
  legacy_replacements+=("$replacement")
done < "$legacy_manifest"

is_canonical_name() {
  local candidate="$1"
  local i
  for ((i=0; i<${#canonical_names[@]}; i++)); do
    if [[ "${canonical_names[$i]}" == "$candidate" ]]; then
      return 0
    fi
  done
  return 1
}

protected_aliases=()

protect_alias() {
  protected_aliases+=("$1")
}

is_protected_alias() {
  local candidate="$1"
  local i
  for ((i=0; i<${#protected_aliases[@]}; i++)); do
    if [[ "${protected_aliases[$i]}" == "$candidate" ]]; then
      return 0
    fi
  done
  return 1
}

protected_previous_default_aliases=()
planned_previous_default_targets=()

protect_previous_default_alias() {
  protected_previous_default_aliases+=("$1")
}

is_protected_previous_default_alias() {
  local candidate="$1"
  local i
  for ((i=0; i<${#protected_previous_default_aliases[@]}; i++)); do
    if [[ "${protected_previous_default_aliases[$i]}" == "$candidate" ]]; then
      return 0
    fi
  done
  return 1
}

plan_previous_default_target() {
  planned_previous_default_targets+=("$1")
}

is_planned_previous_default_target() {
  local candidate="$1"
  local i
  for ((i=0; i<${#planned_previous_default_targets[@]}; i++)); do
    if [[ "${planned_previous_default_targets[$i]}" == "$candidate" ]]; then
      return 0
    fi
  done
  return 1
}

source_bundle_for() {
  local index="$1"
  if [[ "$source_layout" == "flat" ]]; then
    printf '%s/%s\n' "$source_root" "${canonical_names[$index]}"
  else
    printf '%s/%s\n' "$source_root" "${relative_paths[$index]}"
  fi
}

copy_source_bundle() {
  local source_bundle="$1"
  local destination_bundle="$2"

  # ditto preserves bundle metadata and the package-time code signature.
  # --noqtn is retained as a first pass, but some macOS versions still copy
  # quarantine metadata on nested bundle paths. The validated staged copy is
  # therefore cleared explicitly before it is installed.
  /usr/bin/ditto --noqtn "$source_bundle" "$destination_bundle"
}

clear_bundle_quarantine() {
  local bundle="$1"

  # The user has explicitly approved this installer. Remove only Gatekeeper's
  # quarantine attribute, recursively, from this manifest-listed staged bundle.
  # Do not clear other extended attributes or touch the complete CLAP folder.
  /usr/bin/xattr -drs com.apple.quarantine "$bundle"
}

validate_source_bundle() {
  local source_bundle="$1"
  local expected_id="$2"
  local expected_host_name="$3"
  local actual_id actual_host_name executable
  if [[ ! -d "$source_bundle" || -L "$source_bundle" ]]; then
    echo "Missing or unsafe source bundle: $source_bundle" >&2
    return 1
  fi
  if ! actual_id="$(bundle_id "$source_bundle")"; then
    echo "Cannot read CFBundleIdentifier: $source_bundle" >&2
    return 1
  fi
  if [[ "$actual_id" != "$expected_id" ]]; then
    echo "Source identity mismatch: $source_bundle" >&2
    echo "  expected: $expected_id" >&2
    echo "  found:    $actual_id" >&2
    return 1
  fi
  if ! actual_host_name="$(bundle_plist_value "$source_bundle" CFBundleName)"; then
    echo "Cannot read CFBundleName: $source_bundle" >&2
    return 1
  fi
  if [[ "$actual_host_name" != "$expected_host_name" ]]; then
    echo "Source host-name mismatch: $source_bundle" >&2
    echo "  expected: $expected_host_name" >&2
    echo "  found:    $actual_host_name" >&2
    return 1
  fi
  if ! executable="$(bundle_plist_value "$source_bundle" CFBundleExecutable)"; then
    echo "Cannot read CFBundleExecutable: $source_bundle" >&2
    return 1
  fi
  if [[ "$executable" == */* || -z "$executable" \
      || ! -f "$source_bundle/Contents/MacOS/$executable" \
      || ! -x "$source_bundle/Contents/MacOS/$executable" ]]; then
    echo "Missing declared bundle executable: $source_bundle/Contents/MacOS/$executable" >&2
    return 1
  fi
}

validate_destination_canonical() {
  local name="$1"
  local expected_id="$2"
  local path="$destination/$name"
  local actual_id
  if [[ ! -e "$path" && ! -L "$path" ]]; then
    return 0
  fi
  if [[ -L "$path" || ! -d "$path" ]]; then
    echo "Refusing to replace a non-bundle or symlink: $path" >&2
    return 1
  fi
  if ! actual_id="$(bundle_id "$path")"; then
    echo "Refusing to replace a bundle with unreadable identity: $path" >&2
    return 1
  fi
  if [[ "$actual_id" != "$expected_id" ]]; then
    echo "Refusing to replace an unexpected bundle: $path" >&2
    echo "  expected: $expected_id" >&2
    echo "  found:    $actual_id" >&2
    return 1
  fi
}

backup_previous_default_alias() {
  local name="$1"
  local expected_id="$2"
  local replacement="$3"
  local path="$default_clap_root/$name"
  local backup_dir="$backup_run/Previous CLAP Root"
  local actual_id

  if is_protected_previous_default_alias "$name"; then
    return 0
  fi
  if [[ ! -e "$path" && ! -L "$path" ]]; then
    return 0
  fi
  if [[ -L "$path" || ! -d "$path" ]]; then
    warn "leaving non-bundle or symlink in the previous CLAP root untouched: $path"
    return 0
  fi
  if ! actual_id="$(bundle_id "$path")"; then
    warn "leaving bundle with unreadable identity in the previous CLAP root untouched: $path"
    return 0
  fi
  if ! expected_id_matches "$expected_id" "$actual_id"; then
    warn "leaving bundle with unexpected identity in the previous CLAP root untouched: $path ($actual_id)"
    return 0
  fi

  if [[ $dry_run -eq 1 ]]; then
    echo "BACK UP  $path -> $backup_dir/$name ($replacement)"
    protect_previous_default_alias "$name"
  else
    mkdir -p "$backup_dir"
    backup_created=1
    mv "$path" "$backup_dir/$name"
    echo "BACKED UP $path ($replacement)"
  fi
  backed_up_count=$((backed_up_count + 1))
}

migrate_previous_default_alias() {
  local name="$1"
  local expected_id="$2"
  local canonical_name="$3"
  local path="$default_clap_root/$name"
  local canonical_path="$destination/$canonical_name"
  local actual_id

  if is_protected_previous_default_alias "$name"; then
    return 0
  fi
  if [[ ! -e "$path" && ! -L "$path" ]]; then
    return 0
  fi
  if [[ -L "$path" || ! -d "$path" ]]; then
    warn "leaving non-bundle or symlink in the previous CLAP root untouched: $path"
    protect_previous_default_alias "$name"
    return 0
  fi
  if ! actual_id="$(bundle_id "$path")" || [[ "$actual_id" != "$expected_id" ]]; then
    warn "leaving current-name bundle with unexpected identity in the previous CLAP root untouched: $path"
    protect_previous_default_alias "$name"
    return 0
  fi

  if [[ -e "$canonical_path" || -L "$canonical_path" ]] \
      || is_planned_previous_default_target "$canonical_name"; then
    if is_planned_previous_default_target "$canonical_name"; then
      backup_previous_default_alias "$name" "$expected_id" "$canonical_name"
      return 0
    fi
    if ! validate_destination_canonical "$canonical_name" "$expected_id"; then
      warn "cannot migrate $path while the nested canonical destination conflicts"
      protect_previous_default_alias "$name"
      return 0
    fi
    backup_previous_default_alias "$name" "$expected_id" "$canonical_name"
    return 0
  fi

  if [[ $dry_run -eq 1 ]]; then
    echo "RENAME   $path -> $canonical_path"
    protect_previous_default_alias "$name"
    plan_previous_default_target "$canonical_name"
  else
    mkdir -p "$destination"
    mv "$path" "$canonical_path"
    clear_bundle_quarantine "$canonical_path"
    echo "RENAMED  $path -> $canonical_path"
  fi
  migrated_count=$((migrated_count + 1))
}

timestamp="$(date +%Y%m%d-%H%M%S)"
backup_run="$backup_parent/$timestamp-$$"
backup_created=0
backed_up_count=0
warning_count=0
migrated_count=0
installed_count=0

warn() {
  warning_count=$((warning_count + 1))
  echo "WARNING: $*" >&2
}

backup_alias() {
  local name="$1"
  local expected_id="$2"
  local replacement="$3"
  local path="$destination/$name"
  local actual_id

  if is_canonical_name "$name" || is_protected_alias "$name"; then
    return 0
  fi
  if [[ ! -e "$path" && ! -L "$path" ]]; then
    return 0
  fi
  if [[ -L "$path" || ! -d "$path" ]]; then
    warn "leaving non-bundle or symlink alias untouched: $path"
    return 0
  fi
  if ! actual_id="$(bundle_id "$path")"; then
    warn "leaving alias with unreadable identity untouched: $path"
    return 0
  fi
  if ! expected_id_matches "$expected_id" "$actual_id"; then
    warn "leaving alias with unexpected identity untouched: $path ($actual_id)"
    return 0
  fi

  if [[ $dry_run -eq 1 ]]; then
    echo "BACK UP  $name -> $backup_run/$name ($replacement)"
    # Model the planned move so overlapping active, legacy, and receipt entries
    # are reported exactly once during a dry run.
    protect_alias "$name"
  else
    if [[ $backup_created -eq 0 ]]; then
      mkdir -p "$backup_run"
      backup_created=1
    fi
    mv "$path" "$backup_run/$name"
    echo "BACKED UP $name ($replacement)"
  fi
  backed_up_count=$((backed_up_count + 1))
}

read_previous_receipt() {
  previous_names=()
  previous_ids=()
  if [[ -z "$receipt" || ! -f "$receipt" ]]; then
    return 0
  fi
  local name expected_id extra
  while IFS=$'\t' read -r name expected_id extra; do
    if [[ -z "${name:-}" || "${name:0:1}" == "#" ]]; then
      continue
    fi
    if [[ -n "${extra:-}" ]] || ! safe_bundle_name "$name" || [[ "$expected_id" != org.s3g.s3g-dsp.* ]]; then
      warn "ignoring malformed install receipt row: ${name:-<empty>}"
      continue
    fi
    previous_names+=("$name")
    previous_ids+=("$expected_id")
  done < "$receipt"
}

write_receipt() {
  local receipt_dir receipt_temp actual_id i count
  if [[ -z "$receipt" ]]; then
    return 0
  fi
  receipt_dir="$(dirname "$receipt")"
  mkdir -p "$receipt_dir"
  receipt_temp="$receipt.tmp.$$"
  count=0
  {
    printf '# Canonical installed filename\tCLAP/CFBundle identifier\n'
    for ((i=0; i<${#canonical_names[@]}; i++)); do
      if [[ -d "$destination/${canonical_names[$i]}" ]] \
          && actual_id="$(bundle_id "$destination/${canonical_names[$i]}")" \
          && [[ "$actual_id" == "${bundle_ids[$i]}" ]]; then
        printf '%s\t%s\n' "${canonical_names[$i]}" "${bundle_ids[$i]}"
        count=$((count + 1))
      fi
    done
  } > "$receipt_temp"
  mv "$receipt_temp" "$receipt"
  echo "RECEIPT  $receipt ($count canonical bundles)"
}

read_previous_receipt

echo "s3g-dsp CLAP canonical bundle installer"
echo "Manifest:    $manifest"
echo "Destination: $destination"
if [[ $migrate_previous_default -eq 1 ]]; then
  echo "Previous root: $default_clap_root"
fi
if [[ $dry_run -eq 1 ]]; then
  echo "Mode:        dry run"
elif [[ $canonicalize_only -eq 1 ]]; then
  echo "Mode:        canonicalize existing bundles only"
else
  echo "Mode:        install current bundles"
fi
echo "Active set:  ${#canonical_names[@]} bundles"

if [[ $canonicalize_only -eq 0 ]]; then
  # Preflight every source and destination before making any change.
  for ((i=0; i<${#canonical_names[@]}; i++)); do
    source_bundle="$(source_bundle_for "$i")"
    validate_source_bundle "$source_bundle" "${bundle_ids[$i]}" "${host_names[$i]}"
    validate_destination_canonical "${canonical_names[$i]}" "${bundle_ids[$i]}"
  done

  if [[ $dry_run -eq 1 ]]; then
    for ((i=0; i<${#canonical_names[@]}; i++)); do
      source_bundle="$(source_bundle_for "$i")"
      echo "INSTALL  $source_bundle -> $destination/${canonical_names[$i]}"
      installed_count=$((installed_count + 1))
    done
  else
    mkdir -p "$destination"
    stage_root="$(mktemp -d "$destination/.s3g-dsp-install.XXXXXX")"
    cleanup_stage() {
      if [[ -n "${stage_root:-}" && -d "$stage_root" ]]; then
        rm -rf "$stage_root"
      fi
    }
    trap cleanup_stage EXIT

    # Copy the complete set first. A partial source set can never disturb an
    # existing installation.
    for ((i=0; i<${#canonical_names[@]}; i++)); do
      source_bundle="$(source_bundle_for "$i")"
      staged_bundle="$stage_root/${canonical_names[$i]}"
      copy_source_bundle "$source_bundle" "$staged_bundle"
      validate_source_bundle "$staged_bundle" \
        "${bundle_ids[$i]}" "${host_names[$i]}"
      clear_bundle_quarantine "$staged_bundle"
    done

    for ((i=0; i<${#canonical_names[@]}; i++)); do
      canonical_name="${canonical_names[$i]}"
      destination_bundle="$destination/$canonical_name"
      previous_bundle="$stage_root/.previous-$i-$canonical_name"
      if [[ -e "$destination_bundle" ]]; then
        mv "$destination_bundle" "$previous_bundle"
      fi
      if ! mv "$stage_root/$canonical_name" "$destination_bundle"; then
        if [[ -e "$previous_bundle" ]]; then
          mv "$previous_bundle" "$destination_bundle"
        fi
        echo "Failed to install $canonical_name" >&2
        exit 1
      fi
      if [[ -e "$previous_bundle" ]]; then
        rm -rf "$previous_bundle"
      fi
      echo "INSTALLED $canonical_name"
      installed_count=$((installed_count + 1))
    done
  fi
else
  # Preserve the exact installed binaries and signatures; only change outer
  # bundle directory names after checking both source and destination IDs.
  for ((i=0; i<${#canonical_names[@]}; i++)); do
    old_name="$(basename "${relative_paths[$i]}")"
    canonical_name="${canonical_names[$i]}"
    expected_id="${bundle_ids[$i]}"
    old_path="$destination/$old_name"
    canonical_path="$destination/$canonical_name"

    if [[ "$old_name" == "$canonical_name" || ( ! -e "$old_path" && ! -L "$old_path" ) ]]; then
      continue
    fi
    if [[ -L "$old_path" || ! -d "$old_path" ]]; then
      warn "leaving non-bundle or symlink alias untouched: $old_path"
      protect_alias "$old_name"
      continue
    fi
    if ! actual_id="$(bundle_id "$old_path")" || [[ "$actual_id" != "$expected_id" ]]; then
      warn "leaving current-name alias with unexpected identity untouched: $old_path"
      protect_alias "$old_name"
      continue
    fi

    if [[ -e "$canonical_path" || -L "$canonical_path" ]]; then
      if ! validate_destination_canonical "$canonical_name" "$expected_id"; then
        warn "cannot migrate $old_name while the canonical destination conflicts"
        protect_alias "$old_name"
        continue
      fi
      backup_alias "$old_name" "$expected_id" "$canonical_name"
      continue
    fi

    if [[ $dry_run -eq 1 ]]; then
      echo "RENAME   $old_name -> $canonical_name"
      # Model the planned rename so the later alias pass does not also report
      # the same source path as a backup in dry-run output.
      protect_alias "$old_name"
    else
      mkdir -p "$destination"
      mv "$old_path" "$canonical_path"
      echo "RENAMED  $old_name -> $canonical_name"
    fi
    migrated_count=$((migrated_count + 1))
  done
fi

# Every old build output basename is automatically an alias whenever it differs
# from the canonical installed name. This makes future OUTPUT_NAME changes safe
# without requiring another handwritten cleanup entry.
for ((i=0; i<${#canonical_names[@]}; i++)); do
  old_name="$(basename "${relative_paths[$i]}")"
  if [[ "$old_name" != "${canonical_names[$i]}" ]]; then
    backup_alias "$old_name" "${bundle_ids[$i]}" "${canonical_names[$i]}"
  fi
done

for ((i=0; i<${#legacy_names[@]}; i++)); do
  backup_alias "${legacy_names[$i]}" "${legacy_ids[$i]}" "${legacy_replacements[$i]}"
done

# A receipt closes the loop for future renames: names installed by an earlier
# manifest are retired even if they predate the static historical alias list.
for ((i=0; i<${#previous_names[@]}; i++)); do
  if ! is_canonical_name "${previous_names[$i]}"; then
    backup_alias "${previous_names[$i]}" "${previous_ids[$i]}" "renamed in current manifest"
  fi
done

# Earlier installers placed every bundle directly in the user CLAP root. When
# using the new default subfolder, migrate verified current bundles during a
# canonicalize-only run or back up those top-level copies after a normal
# install. This prevents REAPER from discovering duplicate products while
# leaving unrelated CLAP bundles untouched.
if [[ $migrate_previous_default -eq 1 ]]; then
  for ((i=0; i<${#canonical_names[@]}; i++)); do
    canonical_name="${canonical_names[$i]}"
    expected_id="${bundle_ids[$i]}"
    old_name="$(basename "${relative_paths[$i]}")"

    if [[ $canonicalize_only -eq 1 ]]; then
      migrate_previous_default_alias "$canonical_name" "$expected_id" "$canonical_name"
      if [[ "$old_name" != "$canonical_name" ]]; then
        migrate_previous_default_alias "$old_name" "$expected_id" "$canonical_name"
      fi
    else
      backup_previous_default_alias "$canonical_name" "$expected_id" "moved to s3g-dsp subfolder"
      if [[ "$old_name" != "$canonical_name" ]]; then
        backup_previous_default_alias "$old_name" "$expected_id" "$canonical_name"
      fi
    fi
  done

  for ((i=0; i<${#legacy_names[@]}; i++)); do
    backup_previous_default_alias "${legacy_names[$i]}" "${legacy_ids[$i]}" "${legacy_replacements[$i]}"
  done

  for ((i=0; i<${#previous_names[@]}; i++)); do
    if ! is_canonical_name "${previous_names[$i]}"; then
      backup_previous_default_alias "${previous_names[$i]}" "${previous_ids[$i]}" "renamed in current manifest"
    fi
  done
fi

if [[ $dry_run -eq 0 ]]; then
  write_receipt
fi

echo "Summary: installed=$installed_count renamed=$migrated_count backed-up=$backed_up_count warnings=$warning_count"
if [[ $backup_created -eq 1 ]]; then
  echo "Legacy backup: $backup_run"
elif [[ $dry_run -eq 1 && $backed_up_count -gt 0 ]]; then
  echo "Planned legacy backup: $backup_run"
fi

if [[ $warning_count -gt 0 ]]; then
  exit 3
fi
