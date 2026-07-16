#!/usr/bin/env bash
set -u

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
strict=0
verbose=0
for arg in "$@"; do
  case "$arg" in
    --strict) strict=1 ;;
    --verbose) verbose=1 ;;
    -h|--help)
      echo "Usage: scripts/audit-gui-style.sh [--strict] [--verbose]"
      echo "  --strict   exit nonzero if warnings are found"
      echo "  --verbose  print every warning instead of capping each section"
      exit 0
      ;;
    *)
      echo "Unknown option: $arg" >&2
      exit 2
      ;;
  esac
done

if ! command -v rg >/dev/null 2>&1; then
  echo "s3g GUI style audit: ripgrep (rg) is required" >&2
  exit 2
fi

warnings=0
section_count=0
section_omitted=0
section_limit=20

warn() {
  local category="$1"
  local file_line="$2"
  local detail="$3"
  if [[ "$verbose" -eq 1 || "$section_count" -lt "$section_limit" ]]; then
    printf '[%s] %s\n  %s\n' "$category" "$file_line" "$detail"
  else
    section_omitted=$((section_omitted + 1))
  fi
  section_count=$((section_count + 1))
  warnings=$((warnings + 1))
}

section() {
  finish_section
  printf '\n== %s ==\n' "$1"
  section_count=0
  section_omitted=0
}

finish_section() {
  if [[ "$section_omitted" -gt 0 ]]; then
    printf '  ... %d more warning(s) hidden in this section; rerun with --verbose to see all.\n' "$section_omitted"
  fi
}

cd "$repo_root" || exit 2

section "Typography"
while IFS= read -r hit; do
  warn "text" "$hit" "Avoid local bold UI labels. Use shared regular-weight helpers unless this is an explicit icon-like marker."
done < <(rg -n 'Menlo-Bold|NSFontWeightBold' plugins --glob '*.cpp' --glob '!plugins/common/s3g_cocoa_gui.h')

while IFS= read -r hit; do
  warn "text" "$hit" "Avoid pure white default UI text. Prefer softLabelAttrs(), softValueAttrs(), or softTitleAttrs()."
done < <(rg -n 'NSForegroundColorAttributeName[^;\n]*(0xf0f0f0|0xffffff|whiteColor)|setTextColor:[^;\n]*(0xf0f0f0|0xffffff|whiteColor)' plugins --glob '*.cpp' --glob '!plugins/common/s3g_cocoa_gui.h')

section "Peak Readouts"
while IFS= read -r hit; do
  warn "peak" "$hit" "Use s3g::clap_gui::peakDbText() for PK readouts so peak display format is consistent."
done < <(rg -n 'PK %|PK %\+|PK %.|stringWithFormat:@"PK' plugins --glob '*.cpp' --glob '!plugins/common/s3g_cocoa_gui.h')

section "Panel Headers"
while IFS= read -r hit; do
  warn "header" "$hit" "drawPanelHeader() is static. Use drawDisclosurePanelHeader() only when the header click target toggles the panel."
done < <(rg -n 'drawPanelHeader\([^;\n]*(_show|Open|open|_binaural|_transaural|showGlossary)' plugins --glob '*.cpp')

while IFS= read -r hit; do
  warn "header" "$hit" "Manual +/- header drawing should be converted to drawDisclosurePanelHeader() or a static drawPanelHeader()."
done < <(rg -n 'open \? @"-"|open \? @"−"|marker = open' plugins --glob '*.cpp')

section "Control Types"
while IFS= read -r hit; do
  warn "control" "$hit" "Binary BYPASS controls should be buttons/toggles unless a slider is deliberately justified."
done < <(rg -n 'drawSlider\(@"BYPASS"|drawSlider\(@"BYP"' plugins --glob '*.cpp')

while IFS= read -r hit; do
  warn "control" "$hit" "Small stepped ORD controls should usually be dropdown menus, not continuous sliders."
done < <(rg -n 'drawSlider\(@"ORD"|drawSlider\(@"ORDER"' plugins --glob '*.cpp')

while IFS= read -r hit; do
  warn "control" "$hit" "Small stepped POLES controls should use dropdown menus."
done < <(rg -n 'drawSlider\(@"POLES"|drawSlider\(@"POLS"' plugins --glob '*.cpp')

while IFS= read -r hit; do
  warn "control" "$hit" "Avoid system popup controls inside custom CLAP canvases; use shared custom dropdowns."
done < <(rg -n 'NSPopUpButton|NSComboBox|NSMenuItem' plugins --glob '*.cpp')

section "Text Entry"
while IFS= read -r file; do
  if ! rg -q 'styleNumberTextField|styleNumberTextEditor|selectedTextAttributes' "$file"; then
    warn "textfield" "$file" "Editable NSTextField usage should use shared number-field styling or explicitly style dark selection/editing."
  fi
done < <(rg -l 'NSTextField' plugins --glob '*.cpp')

section "Timers"
while IFS= read -r file; do
  if ! rg -q 'hostAppIsActive|guiVisible|stopRefreshTimer|stopTimer|invalidate' "$file"; then
    warn "timer" "$file" "GUI timer found without an obvious hidden/inactive gate or stop/invalidate path."
  fi
done < <(rg -l 'scheduledTimerWithTimeInterval|timerWithTimeInterval' plugins --glob '*.cpp')

section "Spatial View State"
while IFS= read -r file; do
  if rg -q 'TOP|SIDE|3/4|viewButtonRect|setViewPreset' "$file" && ! rg -q 'guiViewMode|viewMode|viewZoom|guiZoom|viewAz|viewEl' "$file"; then
    warn "view" "$file" "Spatial view controls found without obvious saved view/camera state."
  fi
done < <(rg -l 'viewButtonRect|setViewPreset|@"TOP"|@"SIDE"|@"3/4"' plugins --glob '*.cpp')

section "Draw/Hit Geometry"
while IFS= read -r hit; do
  warn "geometry" "$hit" "Magic-number hit rectangles are worth reviewing against draw rect constants."
done < <(rg -c 'NSPointInRect\(pt, NSMakeRect\([0-9]' plugins --glob '*.cpp')

finish_section
printf '\nGUI style audit complete: %d warning(s).\n' "$warnings"
if [[ "$strict" -eq 1 && "$warnings" -gt 0 ]]; then
  exit 1
fi
exit 0
