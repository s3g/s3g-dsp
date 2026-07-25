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

section "Layout Contract"
if ! rg -q 'double contentTop = 42\.0' plugins/common/s3g_gui_layout.h; then
  warn "layout" "plugins/common/s3g_gui_layout.h" \
    "The first toolbox or primary field must begin at the shared y 42 px content top."
fi

translated_content_top_sources=(
  "plugins/clap_ambi_ray_encoder/s3g_ambi_ray_encoder_clap.cpp"
  "plugins/clap_ambi_ray_bilocation_encoder/s3g_ambi_ray_bilocation_encoder_clap.cpp"
  "plugins/clap_ambisonic_head_decoder/s3g_ambisonic_head_decoder_clap.cpp"
  "plugins/clap_ambisonic_stereo_decoder/s3g_ambisonic_stereo_decoder_clap.cpp"
  "plugins/clap_delay_processor/s3g_delay_processor_clap.cpp"
  "plugins/clap_wave_geometry_processor/s3g_wave_geometry_processor_clap.cpp"
  "plugins/clap_spectral_topology_processor/s3g_spectral_topology_processor_clap.cpp"
  "plugins/clap_ambi_grain_processor/s3g_ambi_grain_processor_clap.cpp"
)
for file in "${translated_content_top_sources[@]}"; do
  if ! rg -q 'kStandardMetrics\.contentTop' "$file" \
      || ! rg -q 'translateXBy:0\.0 yBy:k[A-Za-z]*ContentTranslation' "$file"; then
    warn "layout" "$file" \
      "This migrated editor must derive its content translation from the shared y 42 px top edge."
  fi
done

layout_contract_pilots=(
  "plugins/clap_ambi_stochastic_encoder/s3g_ambi_stochastic_encoder_clap.cpp"
  "plugins/clap_ambi_pulsar_encoder/s3g_ambi_pulsar_encoder_clap.cpp"
  "plugins/clap_ambi_wrangler_encoder/s3g_ambi_wrangler_encoder_clap.cpp"
)
for file in "${layout_contract_pilots[@]}"; do
  if ! rg -q 'namespace layout = s3g::gui_layout' "$file"; then
    warn "layout" "$file" "The procedural-encoder reference family must use the shared GUI layout contract."
  fi
  if ! rg -q 'static_assert\(layout::validateColumn\(kFirstColumnPanels' "$file"; then
    warn "layout" "$file" "The first parameter column needs compile-time bounds, alignment, gap, and output-first validation."
  fi
  if ! rg -q 'static_assert\(kGuiSliders\[0\]\.id == kOutputParamId\)' "$file"; then
    warn "layout" "$file" "The reference-family GUI mapping must assert that OUT is the first slider."
  fi
  if ! rg -q 'controlMatchesSlot' "$file" || ! rg -q 'kLargeEncoderOrderSlot' "$file"; then
    warn "layout" "$file" "The reference-family ORDER menu must use the canonical large-encoder family slot."
  fi
  if rg -q 'kTopologyPanel = ' "$file"; then
    if ! rg -q 'kLargeEncoderTopologyAnchor' "$file"; then
      warn "layout" "$file" "A procedural-encoder TOPOLOGY panel must use the conditional second-column anchor."
    fi
    if ! rg -q 'topologyRow\(layout::SharedControlRole::Topology' "$file"; then
      warn "layout" "$file" "Shared TOPOLOGY controls must use the canonical family row order."
    fi
  fi
  if rg -q 'drawPanelFrame\([0-9]' "$file"; then
    warn "layout" "$file" "Reference-family toolbox frames must come from shared Panel geometry, not numeric draw coordinates."
  fi
done

large_encoder_order_family=(
  "plugins/clap_ambi_stochastic_encoder/s3g_ambi_stochastic_encoder_clap.cpp"
  "plugins/clap_ambi_pulsar_encoder/s3g_ambi_pulsar_encoder_clap.cpp"
  "plugins/clap_ambi_wrangler_encoder/s3g_ambi_wrangler_encoder_clap.cpp"
  "plugins/clap_ambi_neural_ecology/s3g_ambi_neural_ecology_clap.cpp"
  "plugins/clap_ambi_insect_encoder/s3g_ambi_insect_encoder_clap.cpp"
  "plugins/clap_ambi_vot_encoder/s3g_ambi_vot_encoder_clap.cpp"
  "plugins/clap_ambi_vox_encoder/s3g_ambi_vox_encoder_clap.cpp"
  "plugins/clap_ambi_water_encoder/s3g_ambi_water_encoder_clap.cpp"
  "plugins/clap_ambi_wave_terrain_encoder/s3g_ambi_wave_terrain_encoder_clap.cpp"
  "plugins/clap_ambi_wind_encoder/s3g_ambi_wind_encoder_clap.cpp"
)
for file in "${large_encoder_order_family[@]}"; do
  if ! rg -q 'kLargeEncoderOrderSlot|@"ORDER"[^\n]*y:104|"ORDER"[^\n]*630, 104' "$file"; then
    warn "layout" "$file" "Large-encoder ORDER must occupy the shared OUTPUT row at x 738 / y 104."
  fi
done

encoder_family_members=(
  "plugins/clap_3oafx_point_encoder/s3g_3oafx_point_encoder_clap.cpp|s3g Ambi Encoder Point"
  "plugins/clap_ambi_cloud_encoder/s3g_ambi_cloud_encoder_clap.cpp|s3g Ambi Encoder Cloud"
  "plugins/clap_ambi_path_encoder/s3g_ambi_path_encoder_clap.cpp|s3g Ambi Encoder Path"
  "plugins/clap_ambi_terrain_navigator/s3g_ambi_terrain_navigator_clap.cpp|s3g Ambi Encoder Surface Terrain"
  "plugins/clap_ambi_ray_encoder/s3g_ambi_ray_encoder_clap.cpp|s3g Ambi Encoder Ray"
  "plugins/clap_ambi_ray_bilocation_encoder/s3g_ambi_ray_bilocation_encoder_clap.cpp|s3g Ambi Encoder Ray Bilocation"
  "plugins/clap_ambi_insect_encoder/s3g_ambi_insect_encoder_clap.cpp|s3g Ambi Encoder Insect"
  "plugins/clap_ambi_neural_ecology/s3g_ambi_neural_ecology_clap.cpp|s3g Ambi Encoder Neural Ecology"
  "plugins/clap_ambi_pulsar_encoder/s3g_ambi_pulsar_encoder_clap.cpp|s3g Ambi Encoder Pulsar"
  "plugins/clap_ambi_stochastic_encoder/s3g_ambi_stochastic_encoder_clap.cpp|s3g Ambi Encoder Stochastic"
  "plugins/clap_ambi_vot_encoder/s3g_ambi_vot_encoder_clap.cpp|s3g Ambi Encoder VOT"
  "plugins/clap_ambi_vox_encoder/s3g_ambi_vox_encoder_clap.cpp|s3g Ambi Encoder Vox"
  "plugins/clap_ambi_water_encoder/s3g_ambi_water_encoder_clap.cpp|s3g Ambi Encoder Water"
  "plugins/clap_ambi_wave_terrain_encoder/s3g_ambi_wave_terrain_encoder_clap.cpp|s3g Ambi Encoder Wave Terrain"
  "plugins/clap_ambi_wind_encoder/s3g_ambi_wind_encoder_clap.cpp|s3g Ambi Encoder Wind"
  "plugins/clap_ambi_wrangler_encoder/s3g_ambi_wrangler_encoder_clap.cpp|s3g Ambi Encoder Wrangler"
)
for member in "${encoder_family_members[@]}"; do
  file="${member%%|*}"
  expected_name="${member#*|}"
  if ! rg -Fq "\"${expected_name}\"" "$file"; then
    warn "name" "$file" "Explicit encoder-family member must expose the host name '${expected_name}'."
  fi
  if ! rg -q 'drawEncoderTitleBand|encoderTitleActionRect' "$file"; then
    warn "layout" "$file" "Encoder title controls must consume the shared compact, medium, or wide title-band geometry."
  fi
  if ! rg -q 'drawEncoderTitleBand|drawEncoderPresetMenu' "$file"; then
    warn "typography" "$file" "Encoder PRESET captions must use the shared title-menu baseline."
  fi
  if ! rg -q 'drawEncoderTitleBand' "$file"; then
    for action in LOAD SAVE RANDOM; do
      if ! rg -q "@\\\"${action}\\\"" "$file"; then
        warn "layout" "$file" "Every encoder title band must expose ${action}."
      fi
    done
  fi
  if ! rg -qi 'loadPluginStatePreset|loadCustomPreset|loadUserPreset' "$file"; then
    warn "layout" "$file" "Every encoder LOAD action must restore a complete user or plugin-state preset."
  fi
  if ! rg -qi 'savePluginStatePreset|saveCustomPreset|saveUserPreset' "$file"; then
    warn "layout" "$file" "Every encoder SAVE action must store a complete user or plugin-state preset."
  fi
  if ! rg -q 'randomButton|randomizeButton|randomRect|EncoderTitleAction::Random' "$file"; then
    warn "layout" "$file" "Every encoder RANDOM action must have shared title geometry and an interaction path."
  fi
  if rg -q '64CH|64 CH' "$file"; then
    warn "layout" "$file" "Encoder title status must not repeat a fixed 64-channel count."
  fi
  if rg --pcre2 -q '"s3g Ambi Encoder [^"]+ 64"' "$file"; then
    warn "name" "$file" "Encoder host names omit the universal 64-channel capability."
  fi
  if ! rg -q 'sliderDoubleClickDefault|\[event clickCount\] >= 2' "$file"; then
    warn "interaction" "$file" "Every Encoder slider surface must implement double-click reset to its declared default."
  fi
done

decoder_family_members=(
  "plugins/clap_ambisonic_head_decoder/s3g_ambisonic_head_decoder_clap.cpp|s3g Ambi Decoder Head"
  "plugins/clap_ambisonic_stereo_decoder/s3g_ambisonic_stereo_decoder_clap.cpp|s3g Ambi Decoder Stereo"
  "plugins/clap_ambisonic_sub_decoder/s3g_ambisonic_sub_decoder_clap.cpp|s3g Ambi Decoder Sub"
  "plugins/clap_ambi_adaptive_decoder/s3g_ambi_adaptive_decoder_clap.cpp|s3g Ambi Decoder Adaptive 64"
  "plugins/clap_ambi_object_decoder/s3g_ambi_object_decoder_clap.cpp|s3g Ambi Decoder Object 64"
  "plugins/clap_3oafx_speaker_decoder/s3g_3oafx_speaker_decoder_clap.cpp|s3g Ambi Decoder Speaker 64"
)
for member in "${decoder_family_members[@]}"; do
  file="${member%%|*}"
  expected_name="${member#*|}"
  if ! rg -Fq "\"${expected_name}\"" "$file"; then
    warn "name" "$file" "Explicit decoder-family member must expose the host name '${expected_name}'."
  fi
  if ! rg -q 'ResponsiveViewport' "$file"; then
    warn "layout" "$file" "Decoder family members must use the shared responsive viewport."
  fi
  if ! rg -q 'drawDecoderTitleBand' "$file"; then
    warn "layout" "$file" "Decoder titles must use the shared Decoder title renderer and established font metrics."
  fi
  if ! rg -q 'loadPluginStatePreset' "$file"; then
    warn "layout" "$file" "Every decoder LOAD action must restore a complete plugin-state preset."
  fi
  if ! rg -q 'savePluginStatePreset' "$file"; then
    warn "layout" "$file" "Every decoder SAVE action must store a complete plugin-state preset."
  fi
  if rg -q 'drawEncoderTitleBand|randomButton|EncoderTitleAction::Random' "$file"; then
    warn "layout" "$file" "Decoder title bands must omit the encoder-family RANDOM action."
  fi
  if rg -q '64CH|64 CH' "$file"; then
    warn "layout" "$file" "Decoder title status must not repeat a fixed 64-channel count."
  fi
  if ! rg -q 'drawPanelHeader\(@"OUTPUT"' "$file"; then
    warn "layout" "$file" "Decoder OUT must live in a dedicated first OUTPUT toolbox."
  fi
  if ! rg -q 'drawMenu[:(]@"ORDER"' "$file"; then
    warn "control" "$file" "Decoder ORDER must use the shared full label and a discrete menu."
  fi
  if ! rg -q 'sliderDoubleClickDefault|\[event clickCount\] >= 2' "$file"; then
    warn "interaction" "$file" "Every Decoder slider surface must implement double-click reset to its declared default."
  fi
done

decoder_output_unit_contracts=(
  'plugins/clap_ambisonic_head_decoder/s3g_ambisonic_head_decoder_clap.cpp|drawSlider:@"OUT"[^\n]*%\+\.1f dB'
  'plugins/clap_ambisonic_stereo_decoder/s3g_ambisonic_stereo_decoder_clap.cpp|drawSlider:@"OUT"[^\n]*%\+\.1f dB'
  'plugins/clap_ambisonic_sub_decoder/s3g_ambisonic_sub_decoder_clap.cpp|drawSlider\(@"OUT"[^\n]*textForParam'
  'plugins/clap_ambi_adaptive_decoder/s3g_ambi_adaptive_decoder_clap.cpp|isEqualToString:@"dB"[^\n]*%\+\.1f dB'
  'plugins/clap_ambi_object_decoder/s3g_ambi_object_decoder_clap.cpp|isEqualToString:@"dB"[^\n]*%\+\.1f dB'
  'plugins/clap_3oafx_speaker_decoder/s3g_3oafx_speaker_decoder_clap.cpp|drawSlider:@"OUT"[^\n]*%\+\.1f dB'
)
for contract in "${decoder_output_unit_contracts[@]}"; do
  file="${contract%%|*}"
  pattern="${contract#*|}"
  if ! rg -q "$pattern" "$file"; then
    warn "typography" "$file" "Decoder OUT values must retain the dB unit in the custom GUI."
  fi
done

if ! rg -q 'menuDisplayText' plugins/common/s3g_cocoa_gui.h \
    || ! rg -q 'uppercaseString' plugins/common/s3g_cocoa_gui.h; then
  warn "typography" "plugins/common/s3g_cocoa_gui.h" \
    "Shared menu rendering must force uppercase display text and fit it to the menu box."
fi

compact_output_first_contracts=(
  'plugins/clap_3oafx_point_encoder/s3g_3oafx_point_encoder_clap.cpp|drawSlider:@"OUT"[^\n]*y:78'
  'plugins/clap_ambi_cloud_encoder/s3g_ambi_cloud_encoder_clap.cpp|drawSlider:@"OUT"[^\n]*y:78'
  'plugins/clap_ambi_path_encoder/s3g_ambi_path_encoder_clap.cpp|drawSlider:@"OUT"[^\n]*y:78'
  'plugins/clap_ambi_terrain_navigator/s3g_ambi_terrain_navigator_clap.cpp|drawSlider:@"OUT"[^\n]*kOutputParamId[^\n]*y:78'
  'plugins/clap_ambi_ray_encoder/s3g_ambi_ray_encoder_clap.cpp|drawSlider:@"OUT"[^\n]*y:70'
  'plugins/clap_ambi_ray_bilocation_encoder/s3g_ambi_ray_bilocation_encoder_clap.cpp|kParamOutput, "OUT", NSMakeRect\(632, 798'
  'plugins/clap_ambi_insect_encoder/s3g_ambi_insect_encoder_clap.cpp|kOutputParamId, 630, 78'
  'plugins/clap_ambi_neural_ecology/s3g_ambi_neural_ecology_clap.cpp|kOutputParamId, "OUT", 630, 78'
  'plugins/clap_ambi_pulsar_encoder/s3g_ambi_pulsar_encoder_clap.cpp|kGuiSliders\[0\]\.id == kOutputParamId'
  'plugins/clap_ambi_stochastic_encoder/s3g_ambi_stochastic_encoder_clap.cpp|kGuiSliders\[0\]\.id == kOutputParamId'
  'plugins/clap_ambi_vot_encoder/s3g_ambi_vot_encoder_clap.cpp|drawSlider:@"OUT"[^\n]*kOutputParamId[^\n]*y:78'
  'plugins/clap_ambi_vox_encoder/s3g_ambi_vox_encoder_clap.cpp|drawSlider:@"OUT"[^\n]*kOutputParamId[^\n]*y:78'
  'plugins/clap_ambi_water_encoder/s3g_ambi_water_encoder_clap.cpp|kOutputParamId, 630, 78'
  'plugins/clap_ambi_wave_terrain_encoder/s3g_ambi_wave_terrain_encoder_clap.cpp|"OUT", kOutputParamId, 630, 78'
  'plugins/clap_ambi_wind_encoder/s3g_ambi_wind_encoder_clap.cpp|kOutputParamId, 630, 78'
  'plugins/clap_ambi_wrangler_encoder/s3g_ambi_wrangler_encoder_clap.cpp|kGuiSliders\[0\]\.id == kOutputParamId'
)
for contract in "${compact_output_first_contracts[@]}"; do
  file="${contract%%|*}"
  pattern="${contract#*|}"
  if ! rg -q "$pattern" "$file"; then
    warn "layout" "$file" "Compact and special encoder slider stacks must place final OUT in their first row or first dedicated output row."
  fi
done

compact_encoder_order_contracts=(
  'plugins/clap_3oafx_point_encoder/s3g_3oafx_point_encoder_clap.cpp|drawMenu:@"ORDER"[^\n]*y:103'
  'plugins/clap_ambi_cloud_encoder/s3g_ambi_cloud_encoder_clap.cpp|drawMenu:@"ORDER"[^\n]*y:104'
  'plugins/clap_ambi_path_encoder/s3g_ambi_path_encoder_clap.cpp|drawMenu:@"ORDER"[^\n]*y:104'
  'plugins/clap_ambi_terrain_navigator/s3g_ambi_terrain_navigator_clap.cpp|drawMenu:@"ORDER"[^\n]*y:104'
  'plugins/clap_ambi_ray_encoder/s3g_ambi_ray_encoder_clap.cpp|drawMenu\(@"ORDER"[^\n]*96'
  'plugins/clap_ambi_ray_bilocation_encoder/s3g_ambi_ray_bilocation_encoder_clap.cpp|kParamOrder, "ORDER", NSMakeRect\(632, 824'
)
for contract in "${compact_encoder_order_contracts[@]}"; do
  file="${contract%%|*}"
  pattern="${contract#*|}"
  if ! rg -q "$pattern" "$file"; then
    warn "layout" "$file" "Compact spatial encoders must keep ORDER immediately after OUT in the output toolbox."
  fi
done

compact_toolbox_alignment_family=(
  "plugins/clap_3oafx_point_encoder/s3g_3oafx_point_encoder_clap.cpp"
  "plugins/clap_ambi_cloud_encoder/s3g_ambi_cloud_encoder_clap.cpp"
)
for file in "${compact_toolbox_alignment_family[@]}"; do
  if ! rg -q 'kStandardMetrics\.labelInset' "$file"; then
    warn "layout" "$file" "Compact encoder control labels must use the shared panel x + 16 px inset."
  fi
  if ! rg -q 'kStandardMetrics\.controlInset' "$file"; then
    warn "layout" "$file" "Compact encoder controls must use the shared panel x + 108 px inset."
  fi
done

if ! rg -q 'softLabelAttrs\(\), small, style' \
    plugins/clap_3oafx_point_encoder/s3g_3oafx_point_encoder_clap.cpp; then
  warn "typography" \
    "plugins/clap_3oafx_point_encoder/s3g_3oafx_point_encoder_clap.cpp" \
    "Point toolbox labels must use the shared label style, not the dimmer value style."
fi
if ! rg -q 'softLabelAttrs\(\), attrs, style' \
    plugins/clap_ambi_ray_encoder/s3g_ambi_ray_encoder_clap.cpp; then
  warn "typography" \
    "plugins/clap_ambi_ray_encoder/s3g_ambi_ray_encoder_clap.cpp" \
    "Ray toolbox labels must use the shared label style, not the dimmer value style."
fi
if ! rg -q 'pointFieldPlotRect' \
    plugins/clap_3oafx_point_encoder/s3g_3oafx_point_encoder_clap.cpp; then
  warn "layout" \
    "plugins/clap_3oafx_point_encoder/s3g_3oafx_point_encoder_clap.cpp" \
    "Point must use an inset dark plotting field inside the shared medium-gray panel shell."
fi
if ! rg -q 'pointPrimaryPanelRect\(\).*' \
    plugins/clap_3oafx_point_encoder/s3g_3oafx_point_encoder_clap.cpp \
    || ! rg -q 'NSMakeRect\(18\.0, 42\.0, 596\.0, 656\.0\)' \
    plugins/clap_3oafx_point_encoder/s3g_3oafx_point_encoder_clap.cpp; then
  warn "layout" \
    "plugins/clap_3oafx_point_encoder/s3g_3oafx_point_encoder_clap.cpp" \
    "Point must share the compact encoder 596 px primary-panel width and one draw/hit geometry source."
fi
if ! rg -q 'positionZPanelRect' \
    plugins/clap_ambi_ray_bilocation_encoder/s3g_ambi_ray_bilocation_encoder_clap.cpp \
    || ! rg -q 'outputPanelRect\(\).*NSMakeRect\(618, 762, 610, 124\)' \
    plugins/clap_ambi_ray_bilocation_encoder/s3g_ambi_ray_bilocation_encoder_clap.cpp; then
  warn "layout" \
    "plugins/clap_ambi_ray_bilocation_encoder/s3g_ambi_ray_bilocation_encoder_clap.cpp" \
    "Ray Bilocation must keep POSITION Z separate and use the balanced wide OUTPUT row."
fi
if ! rg -q 'isParameterMenu\(layout.id\)' \
    plugins/clap_ambi_ray_bilocation_encoder/s3g_ambi_ray_bilocation_encoder_clap.cpp \
    || ! rg -q '_openParameterMenu' \
    plugins/clap_ambi_ray_bilocation_encoder/s3g_ambi_ray_bilocation_encoder_clap.cpp; then
  warn "control" \
    "plugins/clap_ambi_ray_bilocation_encoder/s3g_ambi_ray_bilocation_encoder_clap.cpp" \
    "Ray Bilocation ORDER and MAP must draw and interact as discrete menus."
fi
if ! rg -Fq 'drawPanelHeader(@"SURFACE TERRAIN FIELD"' \
    plugins/clap_ambi_terrain_navigator/s3g_ambi_terrain_navigator_clap.cpp \
    || ! rg -Fq 'NSMakeRect(34, 76, 564, 682)' \
    plugins/clap_ambi_terrain_navigator/s3g_ambi_terrain_navigator_clap.cpp; then
  warn "layout" \
    "plugins/clap_ambi_terrain_navigator/s3g_ambi_terrain_navigator_clap.cpp" \
    "Surface Terrain must use its family identity and fill the footer-free primary field."
fi
if ! rg -Fq 'suffix:@"count"' \
    plugins/clap_ambi_cloud_encoder/s3g_ambi_cloud_encoder_clap.cpp; then
  warn "typography" \
    "plugins/clap_ambi_cloud_encoder/s3g_ambi_cloud_encoder_clap.cpp" \
    "Cloud INPUTS is an integral count and must not display decimal places."
fi
compact_encoder_output_units=(
  "plugins/clap_3oafx_point_encoder/s3g_3oafx_point_encoder_clap.cpp"
  "plugins/clap_ambi_cloud_encoder/s3g_ambi_cloud_encoder_clap.cpp"
  "plugins/clap_ambi_ray_encoder/s3g_ambi_ray_encoder_clap.cpp"
)
for file in "${compact_encoder_output_units[@]}"; do
  if ! rg -Fq '%+.1f dB' "$file"; then
    warn "typography" "$file" "Compact encoder OUT values must retain the dB unit."
  fi
done
if ! rg -q '@"PRESET".*band\.presetLabelX, band\.titleY \+ 1\.0' \
    plugins/common/s3g_cocoa_gui.h; then
  warn "typography" \
    "plugins/common/s3g_cocoa_gui.h" \
    "The shared title-band PRESET caption must align with its menu selection and action text."
fi
if ! rg -q 'clap_gui::drawMenu\(label, value, rect\.origin\.y \+ 1\.0' \
    plugins/clap_ambi_pulsar_encoder/s3g_ambi_pulsar_encoder_clap.cpp; then
  warn "layout" \
    "plugins/clap_ambi_pulsar_encoder/s3g_ambi_pulsar_encoder_clap.cpp" \
    "Pulsar menu labels must use the shared control-baseline renderer."
fi

if ! rg -q 'kEngineControlOrder|kTuningControlOrder|kEnvelopeControlOrder|kProjectionControlOrder|kMotionControlOrder|kListenerControlOrder|kEnvironmentControlOrder' plugins/common/s3g_gui_layout.h; then
  warn "layout" "plugins/common/s3g_gui_layout.h" "Shared encoder family control ordering must remain executable in the global layout contract."
fi

if ! rg -q 'toolboxFirstRowY|toolboxHeightForRows|compactedVisibleRow' plugins/common/s3g_gui_layout.h; then
  warn "layout" "plugins/common/s3g_gui_layout.h" "The global contract must keep executable first-row, content-fitted height, and hidden-row compaction rules."
fi
if ! rg -q 'toolboxBottomClearance = 18\.0' plugins/common/s3g_gui_layout.h; then
  warn "layout" "plugins/common/s3g_gui_layout.h" "All ordinary encoder toolboxes must share the 18 px final-row bottom clearance."
fi

contextual_encoder_layouts=(
  "plugins/clap_ambi_terrain_navigator/s3g_ambi_terrain_navigator_clap.cpp"
  "plugins/clap_ambi_wave_terrain_encoder/s3g_ambi_wave_terrain_encoder_clap.cpp"
  "plugins/clap_ambi_vox_encoder/s3g_ambi_vox_encoder_clap.cpp"
  "plugins/clap_ambi_insect_encoder/s3g_ambi_insect_encoder_clap.cpp"
)
for file in "${contextual_encoder_layouts[@]}"; do
  if ! rg -q 'toolboxHeightForRows|toolboxRowY|guiSliderY' "$file"; then
    warn "layout" "$file" "Contextual encoder panels must calculate visible row geometry instead of reserving hidden rows."
  fi
done

if ! rg -q 'drawBoundedRightText\(value' plugins/common/s3g_cocoa_gui.h; then
  warn "geometry" "plugins/common/s3g_cocoa_gui.h" "Shared slider values must be rounded and drawn inside a bounded right-aligned cell."
fi

section "Host Browser Names"
while IFS= read -r hit; do
  warn "name" "$hit" "Every literal CLAP descriptor display name must begin with 's3g '."
done < <(perl -0777 -ne '
  while (/clap_plugin_descriptor_t\s+\w+\s*\{\s*CLAP_VERSION_INIT\s*,\s*"[^"]+"\s*,\s*"([^"]+)"/sg) {
    print "$ARGV: $1\n" unless $1 =~ /^s3g /;
  }
' plugins/*/*.cpp)

while IFS= read -r hit; do
  warn "name" "$hit" "Ambisonic host names use 's3g Ambi Encoder <member>' so the encoder family sorts together."
done < <(rg -n '"s3g Ambi [^"]+ Encoder(?: [0-9]+)?"' plugins --glob '*.cpp' --glob 'CMakeLists.txt')

while IFS= read -r hit; do
  warn "name" "$hit" "Ambisonic host names use 's3g Ambi Decoder <member>' so the decoder family sorts together."
done < <(rg -n '"s3g Ambi [^"]+ Decoder(?: [0-9]+)?"' plugins --glob '*.cpp' --glob 'CMakeLists.txt')

while IFS= read -r hit; do
  warn "name" "$hit" "Direct panner host names use 's3g Panner <method>'."
done < <(rg -n '"s3g (DBAP|LBAP|VBAP|Layout) Panner"' plugins --glob '*.cpp' --glob 'CMakeLists.txt')

processor_family_names=(
  'plugins/clap_delay_processor/CMakeLists.txt|s3g Processor Delay 8ch'
  'plugins/clap_delay_processor/CMakeLists.txt|s3g Processor Delay 24ch'
  'plugins/clap_buffer_processor/s3g_buffer_processor_clap.cpp|s3g Processor Buffer 8ch'
  'plugins/clap_wave_geometry_processor/CMakeLists.txt|s3g Processor Wave Geometry 8ch'
  'plugins/clap_loop_processor/s3g_loop_processor_clap.cpp|s3g Processor Loop 8ch'
  'plugins/clap_multi_loop_processor/s3g_multi_loop_processor_clap.cpp|s3g Processor Multi Loop 8ch'
  'plugins/clap_psd_raw_field/s3g_psd_raw_field_clap.cpp|s3g Processor Fault 8ch'
  'plugins/clap_ambi_grain_processor/s3g_ambi_grain_processor_clap.cpp|s3g Processor Ambi Grain 16ch'
  'plugins/clap_spectral_topology_processor/CMakeLists.txt|s3g Processor Spectral 8ch'
  'plugins/clap_spectral_topology_processor/CMakeLists.txt|s3g Processor Spectral 24ch'
)
for contract in "${processor_family_names[@]}"; do
  file="${contract%%|*}"
  expected_name="${contract#*|}"
  if ! rg -Fq "\"${expected_name}\"" "$file"; then
    warn "name" "$file" "Processor-family host names must expose '${expected_name}'."
  fi
done

processor_family_sources=(
  plugins/clap_delay_processor/s3g_delay_processor_clap.cpp
  plugins/clap_buffer_processor/s3g_buffer_processor_clap.cpp
  plugins/clap_wave_geometry_processor/s3g_wave_geometry_processor_clap.cpp
  plugins/clap_loop_processor/s3g_loop_processor_clap.cpp
  plugins/clap_multi_loop_processor/s3g_multi_loop_processor_clap.cpp
  plugins/clap_psd_raw_field/s3g_psd_raw_field_clap.cpp
  plugins/clap_ambi_grain_processor/s3g_ambi_grain_processor_clap.cpp
  plugins/clap_spectral_topology_processor/s3g_spectral_topology_processor_clap.cpp
)
for file in "${processor_family_sources[@]}"; do
  if ! rg -Fq 'drawProcessorTitleBand(' "$file"; then
    warn "family" "$file" "Processor editors use the shared PRESET / LOAD / SAVE title renderer."
  fi
  if ! rg -Fq 'handleProcessorTitleClick(' "$file"; then
    warn "family" "$file" "Processor title actions must share complete-state INIT / LOAD / SAVE handling."
  fi
  if ! rg -Fq 'sliderDoubleClickDefault(' "$file"; then
    warn "family" "$file" "Every Processor continuous slider must double-click to its declared default."
  fi
  if ! rg -Fq 'drawProcessorSlider(' "$file"; then
    warn "layout" "$file" "Processor slider rows must use the shared label, track, and bounded-value anchors."
  fi
  if ! rg -Fq 'ResponsiveViewport' "$file"; then
    warn "family" "$file" "Processor editors use the shared responsive viewport."
  fi
  if ! rg -Fq '@"OUTPUT"' "$file" || ! rg -Fq '@"OUT"' "$file"; then
    warn "family" "$file" "Processor control stacks begin with OUTPUT and OUT."
  fi
  if ! rg -q 'PROCESSOR [^"]+([0-9]+|%u)CH' "$file"; then
    warn "name" "$file" "Processor GUI titles include the meaningful channel count."
  fi
  if rg -q '· [0-9]+(CH|OUT)|titleStatus[^\n]*(CH|OUT)' "$file"; then
    warn "layout" "$file" "Processor title status reserves the far-right edge for PK; channel count belongs in the full title."
  fi
done

if ! rg -q 'NSRectFill\(NSMakeRect\(x, y, w, 2\.0\)\)' \
    plugins/common/s3g_cocoa_gui.h; then
  warn "layout" "plugins/common/s3g_cocoa_gui.h" \
    "Every shared toolbox and primary field frame must draw the same top-edge accent separator."
fi

while IFS= read -r hit; do
  warn "name" "$hit" "Processor host names use 's3g Processor <member>' so the family sorts together."
done < <(rg --pcre2 -n '"s3g (?!Processor\b)[^"]+ Processor(?: [0-9]+ch)?"' \
  plugins --glob '*.cpp' --glob 'CMakeLists.txt')

while IFS= read -r hit; do
  warn "name" "$hit" "Literal macOS bundle display names must begin with the s3g family prefix."
done < <(rg --pcre2 -n 'MACOSX_BUNDLE_BUNDLE_NAME\s+"(?!s3g |\$\{)' plugins --glob 'CMakeLists.txt')

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
