#!/usr/bin/env bash
# Scoped native build installation, with complete recoverable pre-update copies.
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$script_dir/.." && pwd)"
export S3G_CLAP_MANIFEST="$script_dir/clap-vstgui-complex-processors.tsv"
export S3G_CLAP_LEGACY_MANIFEST="$script_dir/clap-vstgui-menu-rollout-legacy.tsv"
export S3G_CLAP_SOURCE_ROOT="$repo/build-clap-sample-vstgui-fidelity/plugins"
export S3G_CLAP_SOURCE_LAYOUT=build
export S3G_CLAP_DESTINATION="/Users/s3g/Library/Audio/Plug-Ins/CLAP/s3g-dsp"
export S3G_CLAP_BACKUP_ROOT="/Users/s3g/Library/Application Support/s3g-dsp/CLAP Backups"
export S3G_CLAP_RECEIPT="/Users/s3g/Library/Application Support/s3g-dsp/clap-vstgui-complex-processors-receipt.tsv"

if [[ $# -gt 1 || ( $# -eq 1 && "$1" != --dry-run ) ]]; then
  echo "Usage: bash scripts/install-vstgui-complex-processors.sh [--dry-run]" >&2
  exit 2
fi
# The canonical installer checks every bundle's identity and safe path first.
bash "$script_dir/install-clap-bundles.sh" --dry-run
if [[ $# -eq 1 ]]; then
  exit 0
fi
while IFS=$'\t' read -r relative canonical plugin_id host_name; do
  [[ -z "$relative" || "$relative" == \#* ]] && continue
  codesign --verify --deep --strict "$S3G_CLAP_SOURCE_ROOT/$relative"
done < "$S3G_CLAP_MANIFEST"

mkdir -p "$S3G_CLAP_BACKUP_ROOT"
backup_run="$(mktemp -d "$S3G_CLAP_BACKUP_ROOT/complex-processors.XXXXXX")"
while IFS=$'\t' read -r relative canonical plugin_id host_name; do
  [[ -z "$relative" || "$relative" == \#* ]] && continue
  installed="$S3G_CLAP_DESTINATION/$canonical"
  if [[ -d "$installed" ]]; then
    ditto "$installed" "$backup_run/$canonical"
    diff -qr "$installed" "$backup_run/$canonical"
  fi
done < "$S3G_CLAP_MANIFEST"
ditto "$S3G_CLAP_MANIFEST" "$backup_run/UPDATED_PLUGINS.tsv"
if [[ -f "$S3G_CLAP_RECEIPT" ]]; then
  ditto "$S3G_CLAP_RECEIPT" "$backup_run/PREVIOUS_SCOPED_RECEIPT.tsv"
fi
echo "Recoverable pre-update bundles: $backup_run"
bash "$script_dir/install-clap-bundles.sh"
while IFS=$'\t' read -r relative canonical plugin_id host_name; do
  [[ -z "$relative" || "$relative" == \#* ]] && continue
  installed="$S3G_CLAP_DESTINATION/$canonical"
  codesign --verify --deep --strict "$installed"
  diff -qr "$S3G_CLAP_SOURCE_ROOT/$relative" "$installed"
done < "$S3G_CLAP_MANIFEST"
echo "Verified all scoped installed bundles against their signed build sources."
echo "Backup: $backup_run"
