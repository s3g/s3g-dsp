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

while IFS= read -r hit; do
  warn "header" "$hit" \
    "Parameter toolboxes stay visible. Use a static header and a second responsive column when the control stack is tall; reserve disclosure for optional help/reference bodies."
done < <(rg -n 'drawDisclosurePanelHeader\([^;\n]*(@"(OUTPUT|ENGINE|SPECTRAL ENGINE|WAVE ENGINE|TOPOLOGY|PATCH MATRIX|RELATIONSHIPS|BINAURAL|TRANSAURAL)"|title,)' \
  plugins --glob '*.cpp')

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
  "plugins/clap_ambi_pyrosphere_encoder/s3g_ambi_pyrosphere_encoder_clap.cpp"
  "plugins/clap_ambi_cryosphere_encoder/s3g_ambi_cryosphere_encoder_clap.cpp"
  "plugins/clap_ambi_wave_terrain_encoder/s3g_ambi_wave_terrain_encoder_clap.cpp"
  "plugins/clap_ambi_wind_encoder/s3g_ambi_wind_encoder_clap.cpp"
)
for file in "${large_encoder_order_family[@]}"; do
  if ! rg -q 'kLargeEncoderOrderSlot|@"ORDER"[^\n]*y:104|"ORDER"[^\n]*630, 104' "$file"; then
    warn "layout" "$file" "Large-encoder ORDER must occupy the shared OUTPUT row at x 738 / y 104."
  fi
done

encoder_family_members=(
  "plugins/clap_ambi_point_encoder/s3g_ambi_point_encoder_clap.cpp|s3g Ambi Encoder Point"
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
  "plugins/clap_ambi_pyrosphere_encoder/s3g_ambi_pyrosphere_encoder_clap.cpp|s3g Ambi Encoder Pyrosphere"
  "plugins/clap_ambi_cryosphere_encoder/s3g_ambi_cryosphere_encoder_clap.cpp|s3g Ambi Encoder Cryosphere"
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
  "plugins/clap_ambi_speaker_decoder/s3g_ambi_speaker_decoder_clap.cpp|s3g Ambi Decoder Speaker 64"
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
  'plugins/clap_ambi_speaker_decoder/s3g_ambi_speaker_decoder_clap.cpp|drawSlider:@"OUT"[^\n]*%\+\.1f dB'
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
  'plugins/clap_ambi_point_encoder/s3g_ambi_point_encoder_clap.cpp|drawSlider:@"OUT"[^\n]*y:78'
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
  'plugins/clap_ambi_pyrosphere_encoder/s3g_ambi_pyrosphere_encoder_clap.cpp|kOutputParamId, 630, 78'
  'plugins/clap_ambi_cryosphere_encoder/s3g_ambi_cryosphere_encoder_clap.cpp|kOutputParamId, 630, 78'
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
  'plugins/clap_ambi_point_encoder/s3g_ambi_point_encoder_clap.cpp|drawMenu:@"ORDER"[^\n]*y:103'
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
  "plugins/clap_ambi_point_encoder/s3g_ambi_point_encoder_clap.cpp"
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
    plugins/clap_ambi_point_encoder/s3g_ambi_point_encoder_clap.cpp; then
  warn "typography" \
    "plugins/clap_ambi_point_encoder/s3g_ambi_point_encoder_clap.cpp" \
    "Point toolbox labels must use the shared label style, not the dimmer value style."
fi
if ! rg -q 'softLabelAttrs\(\), attrs, style' \
    plugins/clap_ambi_ray_encoder/s3g_ambi_ray_encoder_clap.cpp; then
  warn "typography" \
    "plugins/clap_ambi_ray_encoder/s3g_ambi_ray_encoder_clap.cpp" \
    "Ray toolbox labels must use the shared label style, not the dimmer value style."
fi
if ! rg -q 'pointFieldPlotRect' \
    plugins/clap_ambi_point_encoder/s3g_ambi_point_encoder_clap.cpp; then
  warn "layout" \
    "plugins/clap_ambi_point_encoder/s3g_ambi_point_encoder_clap.cpp" \
    "Point must use an inset dark plotting field inside the shared medium-gray panel shell."
fi
if ! rg -q 'pointPrimaryPanelRect\(\).*' \
    plugins/clap_ambi_point_encoder/s3g_ambi_point_encoder_clap.cpp \
    || ! rg -q 'NSMakeRect\(18\.0, 42\.0, 596\.0, 656\.0\)' \
    plugins/clap_ambi_point_encoder/s3g_ambi_point_encoder_clap.cpp; then
  warn "layout" \
    "plugins/clap_ambi_point_encoder/s3g_ambi_point_encoder_clap.cpp" \
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
  "plugins/clap_ambi_point_encoder/s3g_ambi_point_encoder_clap.cpp"
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

wave_terrain_source="plugins/clap_ambi_wave_terrain_encoder/s3g_ambi_wave_terrain_encoder_clap.cpp"
wave_terrain_engine="dsp/s3g_ambi_wave_terrain_encoder.h"
scale_catalog="dsp/s3g_musical_scales.h"
scale_menu_sources=(
  "$wave_terrain_source"
  plugins/clap_ambi_vot_encoder/s3g_ambi_vot_encoder_clap.cpp
  plugins/clap_ambi_vox_encoder/s3g_ambi_vox_encoder_clap.cpp
)
if ! rg -q 'kMusicalScaleCount = 101u' "$scale_catalog" \
    || ! rg -q 'kMusicalScaleMenuOrder' "$scale_catalog" \
    || ! rg -q '"PENTATONIC MAJOR"' \
        tests/dsp_smoke.cpp \
    || ! rg -q '"PENTATONIC MINOR"' \
        tests/dsp_smoke.cpp \
    || ! rg -q 'kAmbiWaveTerrainPitchScaleCount' \
        "$wave_terrain_engine" \
    || ! rg -q 'kMusicalScaleCount \+ 1u' \
        "$wave_terrain_engine" \
    || ! rg -q 'openMenuHit' "$wave_terrain_source"; then
  warn "layout" "$wave_terrain_source" \
    "Musical scale menus must retain the complete shared catalog, stable IDs, and canonical family ordering."
fi
for file in "${scale_menu_sources[@]}"; do
  if ! rg -q 'kMusicalScaleCount' "$file" \
      || ! rg -q 'musicalScaleValueForMenuIndex' "$file" \
      || ! rg -q 'drawMultiColumnDropdownMenu' "$file"; then
    warn "control" "$file" \
      "Wave Terrain, VOT, and VOX must expose the same bounded, canonically ordered musical scale catalog."
  fi
done
if rg -q 'TerrainInterpretation::Vector|@"VECTOR"|"VECTOR"' \
    "$wave_terrain_engine" "$wave_terrain_source"; then
  warn "control" "$wave_terrain_source" \
    "Wave Terrain VECTOR interpretation has been removed and must not return to the host or GUI menu."
fi
if ! rg -q 'id == kVoicesParamId.*"%\.0f"' "$wave_terrain_source" \
    || rg -q 'kTriggerModeParamId|AmbiWaveTerrainTriggerMode|@"TRIGGER"|"TRIGGER",' \
        "$wave_terrain_source" "$wave_terrain_engine"; then
  warn "control" "$wave_terrain_source" \
    "Wave Terrain must keep integer VOICES text and expose no separate trigger-mode parameter."
fi
if ! rg -q 'displaySurfacePointU' "$wave_terrain_source" \
    || ! rg -q '_viewDidDrag' "$wave_terrain_source" \
    || ! rg -q '_pendingVoice' "$wave_terrain_source"; then
  warn "view" "$wave_terrain_source" \
    "Wave Terrain must draw the complete closed terrain domain and reserve pointer movement for camera drag."
fi

section "Array Family"
array_family_sources=(
  plugins/clap_array_hpf/s3g_array_hpf_clap.cpp
  plugins/clap_array_delay/s3g_array_delay_clap.cpp
  plugins/clap_array_trim/s3g_array_trim_clap.cpp
)
for file in "${array_family_sources[@]}"; do
  if ! rg -q 'kArrayFamilyLayout' "$file" \
      || ! rg -q 'ResponsiveViewport' "$file"; then
    warn "layout" "$file" \
      "Every Array utility must consume the shared responsive 720 x 388 family layout."
  fi
  if ! rg -q 'drawArrayTitleBand' "$file" \
      || ! rg -q 'handleProcessorTitleClick' "$file" \
      || ! rg -q 'peakDbText' "$file"; then
    warn "title" "$file" \
      "Array titles must expose aligned PRESET, LOAD, SAVE, and far-right PK controls."
  fi
  if ! rg -q 'drawPanel\(@"OUTPUT"' "$file" \
      || ! rg -q '@\"OUT\"' "$file" \
      || ! rg -q 'drawPanel\(@"ARRAY"' "$file" \
      || ! rg -q '@\"ACTIVE\"' "$file"; then
    warn "layout" "$file" \
      "Array utilities keep OUTPUT/OUT at top left and integer ACTIVE in the top-right ARRAY toolbox."
  fi
  if ! rg -q 'sliderDoubleClickDefault' "$file"; then
    warn "control" "$file" \
      "Every Array slider, including channel rows, must support double-click default reset."
  fi
  if rg -q 'drawEncoderTitleBand|@\"RANDOM\"' "$file"; then
    warn "title" "$file" \
      "Array title actions are PRESET / LOAD / SAVE only; RANDOM belongs to Encoders."
  fi
done
array_title_contracts=(
  'plugins/clap_array_hpf/s3g_array_hpf_clap.cpp|s3g ARRAY HPF %uCH'
  'plugins/clap_array_delay/s3g_array_delay_clap.cpp|s3g ARRAY DELAY %uCH'
  'plugins/clap_array_trim/s3g_array_trim_clap.cpp|s3g ARRAY TRIM %uCH'
)
for contract in "${array_title_contracts[@]}"; do
  file="${contract%%|*}"
  title="${contract#*|}"
  if ! rg -Fq "$title" "$file"; then
    warn "title" "$file" \
      "Array titles must keep the meaningful channel-count suffix on the left."
  fi
done
if ! rg -q 'kArrayFamilyLayout' plugins/common/s3g_gui_layout.h \
    || ! rg -Fq '{ 720.0, 388.0 }' plugins/common/s3g_gui_layout.h \
    || ! rg -q 'constexpr const auto& kArray = layout::kArrayFamilyLayout' \
        tests/gui_layout_contract_smoke.cpp \
    || ! rg -q 'processorSliderFitsPanel\(kArray\.' \
        tests/gui_layout_contract_smoke.cpp; then
  warn "layout" "plugins/common/s3g_gui_layout.h" \
    "The Array contract must retain its common canvas, top toolboxes, bounded values, and eight-row editor geometry."
fi

section "Ambi Transform Family"
transform_family_sources=(
  plugins/clap_ambisonic_rotate/s3g_ambisonic_rotate_clap.cpp
  plugins/clap_ambi_group_rotate/s3g_ambi_group_rotate_clap.cpp
  plugins/clap_ambi_group_depth/s3g_ambi_group_depth_clap.cpp
  plugins/clap_ambisonic_order_band_tool/s3g_ambisonic_order_band_tool_clap.cpp
)
for file in "${transform_family_sources[@]}"; do
  if ! rg -q 'kTransformFamilyLayout' "$file" \
      || ! rg -q 'ResponsiveViewport' "$file"; then
    warn "layout" "$file" \
      "Every Ambi Transform consumes the shared responsive 820 x 496 family layout."
  fi
  if ! rg -q 'drawTransformTitleBand' "$file" \
      || ! rg -q 'handleProcessorTitleClick' "$file" \
      || ! rg -q 'peakDbText' "$file"; then
    warn "title" "$file" \
      "Transform titles expose aligned PRESET, LOAD, SAVE, and far-right PK controls."
  fi
  if ! rg -q '@\"OUTPUT\"' "$file" \
      || ! rg -q '@\"OUT\"' "$file"; then
    warn "layout" "$file" \
      "Every Transform keeps a dedicated top-right OUTPUT toolbox with OUT first."
  fi
  if ! rg -q 'sliderDoubleClickDefault' "$file"; then
    warn "control" "$file" \
      "Every Transform continuous slider must support double-click default reset."
  fi
  if rg -q 'drawEncoderTitleBand|@\"RANDOM\"' "$file"; then
    warn "title" "$file" \
      "Transform title actions are PRESET / LOAD / SAVE only."
  fi
done
transform_family_names=(
  'plugins/clap_ambisonic_rotate/CMakeLists.txt|s3g Ambi Transform Rot 64'
  'plugins/clap_ambi_group_rotate/CMakeLists.txt|s3g Ambi Transform Grp Rot 64'
  'plugins/clap_ambi_group_rotate/CMakeLists.txt|s3g Ambi Transform Grp Rot 128'
  'plugins/clap_ambi_group_depth/CMakeLists.txt|s3g Ambi Transform Depth 16'
  'plugins/clap_ambi_group_depth/CMakeLists.txt|s3g Ambi Transform Grp Depth 64'
  'plugins/clap_ambi_group_depth/CMakeLists.txt|s3g Ambi Transform Grp Depth 128'
  'plugins/clap_ambisonic_order_band_tool/CMakeLists.txt|s3g Ambi Transform Order Band 64'
)
for contract in "${transform_family_names[@]}"; do
  file="${contract%%|*}"
  expected_name="${contract#*|}"
  if ! rg -Fq "\"${expected_name}\"" "$file"; then
    warn "name" "$file" \
      "Transform-family host names must expose '${expected_name}'."
  fi
done
if ! rg -q 'kTransformFamilyLayout' plugins/common/s3g_gui_layout.h \
    || ! rg -Fq '{ 820.0, 496.0 }' plugins/common/s3g_gui_layout.h \
    || ! rg -q 'constexpr const auto& kTransform = layout::kTransformFamilyLayout' \
        tests/gui_layout_contract_smoke.cpp; then
  warn "layout" "plugins/common/s3g_gui_layout.h" \
    "The Transform contract must retain its common canvas, field, output, and right-toolbox anchors."
fi

section "Matrix Family"
matrix_family_sources=(
  plugins/clap_group_matrix/s3g_group_matrix_clap.cpp
  plugins/clap_group_matrix_32/s3g_group_matrix_32_clap.cpp
  plugins/clap_ambi_group_matrix/s3g_ambi_group_matrix_clap.cpp
  plugins/clap_ambi_group_matrix_128/s3g_ambi_group_matrix_128_clap.cpp
)
for file in "${matrix_family_sources[@]}"; do
  if ! rg -q 'kMatrixFamilyLayout' "$file" \
      || ! rg -q 'ResponsiveViewport' "$file"; then
    warn "layout" "$file" \
      "Every Matrix consumes the shared responsive 1040 x 648 family layout."
  fi
  if ! rg -q 'drawMatrixTitleBand' "$file" \
      || ! rg -q 'handleProcessorTitleClick' "$file" \
      || ! rg -q 'titleBand.randomButton' "$file" \
      || ! rg -q 'peakDbText' "$file"; then
    warn "title" "$file" \
      "Matrix titles expose aligned PRESET, LOAD, SAVE, RANDOM, and far-right PK controls."
  fi
  if rg -q 'randomButtonRect|@"RAND"' "$file"; then
    warn "title" "$file" \
      "Matrix randomization belongs only in the shared title-band RANDOM action."
  fi
  if ! rg -q 'drawPanel\(@\"OUTPUT\"' "$file" \
      || ! rg -q '@\"OUT\"' "$file" \
      || ! rg -q 'drawPanel\(@\"PATTERN\"' "$file"; then
    warn "layout" "$file" \
      "Every Matrix keeps OUTPUT above the common PATTERN control stack."
  fi
  if ! rg -q 'sliderDoubleClickDefault' "$file"; then
    warn "control" "$file" \
      "Every Matrix slider and crosspoint must support double-click default reset."
  fi
done
matrix_family_names=(
  'plugins/clap_group_matrix_32/CMakeLists.txt|s3g Matrix Group 32'
  'plugins/clap_group_matrix/CMakeLists.txt|s3g Matrix Group 64'
  'plugins/clap_ambi_group_matrix/CMakeLists.txt|s3g Ambi Matrix Group 64'
  'plugins/clap_ambi_group_matrix_128/CMakeLists.txt|s3g Ambi Matrix Group 128'
)
for contract in "${matrix_family_names[@]}"; do
  file="${contract%%|*}"
  expected_name="${contract#*|}"
  if ! rg -Fq "\"${expected_name}\"" "$file"; then
    warn "name" "$file" \
      "Matrix-family host names must expose '${expected_name}'."
  fi
done
if ! rg -q 'kMatrixFamilyLayout' plugins/common/s3g_gui_layout.h \
    || ! rg -Fq '{ 1040.0, 648.0 }' plugins/common/s3g_gui_layout.h \
    || ! rg -q 'constexpr const auto& kMatrix = layout::kMatrixFamilyLayout' \
        tests/gui_layout_contract_smoke.cpp \
    || ! rg -q 'encoderTitleBandFits\(kMatrixTitle\)' \
        tests/gui_layout_contract_smoke.cpp \
    || ! rg -q 'processorSliderFitsPanel\(kMatrix\.' \
        tests/gui_layout_contract_smoke.cpp; then
  warn "layout" "plugins/common/s3g_gui_layout.h" \
    "The Matrix contract must retain its common canvas, top OUTPUT, PATTERN rows, and bounded slider values."
fi

section "Mixer Family"
mixer_family_source=plugins/clap_node_track_mixer/s3g_node_track_mixer_clap.cpp
if ! rg -q 'kMixerFamilyLayout' "$mixer_family_source" \
    || ! rg -q 'ResponsiveViewport' "$mixer_family_source"; then
  warn "layout" "$mixer_family_source" \
    "Both Node Bus variants must consume the shared responsive 920 x 920 Mixer layout."
fi
if ! rg -q 'drawMixerTitleBand' "$mixer_family_source" \
    || ! rg -q 'handleProcessorTitleClick' "$mixer_family_source" \
    || ! rg -q 'peakDbText' "$mixer_family_source"; then
  warn "title" "$mixer_family_source" \
    "Mixer titles expose aligned PRESET, LOAD, SAVE, and far-right PK controls."
fi
if ! rg -Fq '@"OUTPUT"' "$mixer_family_source" \
    || ! rg -Fq '@"BUS / CURSOR"' "$mixer_family_source" \
    || ! rg -Fq '@"OUT"' "$mixer_family_source" \
    || ! rg -Fq '@"ACTIVE"' "$mixer_family_source"; then
  warn "layout" "$mixer_family_source" \
    "Mixers keep OUT in a dedicated first toolbox and separate cursor controls from selected-node controls."
fi
if ! rg -q 'sliderDoubleClickDefault' "$mixer_family_source"; then
  warn "control" "$mixer_family_source" \
    "Every continuous Mixer slider must reset through its declared CLAP default."
fi
if rg -q 'drawEncoderTitleBand|@"RANDOM"' "$mixer_family_source"; then
  warn "title" "$mixer_family_source" \
    "Mixer title actions are PRESET / LOAD / SAVE only."
fi
mixer_family_names=(
  'plugins/clap_node_track_mixer/CMakeLists.txt|s3g Mixer Node Bus 128'
  'plugins/clap_node_track_mixer/CMakeLists.txt|s3g Ambi Mixer Node Bus 128'
)
for contract in "${mixer_family_names[@]}"; do
  file="${contract%%|*}"
  expected_name="${contract#*|}"
  if ! rg -Fq "\"${expected_name}\"" "$file"; then
    warn "name" "$file" \
      "Mixer-family host names must expose '${expected_name}'."
  fi
done
if ! rg -q 'kMixerFamilyLayout' plugins/common/s3g_gui_layout.h \
    || ! rg -Fq '{ 920.0, 920.0 }' plugins/common/s3g_gui_layout.h \
    || ! rg -q 'constexpr const auto& kMixer = layout::kMixerFamilyLayout' \
        tests/gui_layout_contract_smoke.cpp \
    || ! rg -q 'processorSliderFitsPanel\(kMixer\.' \
        tests/gui_layout_contract_smoke.cpp; then
  warn "layout" "plugins/common/s3g_gui_layout.h" \
    "The Mixer contract must retain its field, top OUTPUT, compacted variant rows, and bounded value cells."
fi

section "Macro Family"
macro_family_names=(
  "plugins/clap_macro_delay/CMakeLists.txt|s3g Macro Delay 8ch"
  "plugins/clap_macro_delay/CMakeLists.txt|s3g Macro Delay 24ch"
  "plugins/clap_macro_pitch/CMakeLists.txt|s3g Macro Pitch 8ch"
  "plugins/clap_macro_pitch/CMakeLists.txt|s3g Macro Pitch 24ch"
  "plugins/clap_macro_shred/CMakeLists.txt|s3g Macro Shred Mono"
  "plugins/clap_macro_shred/CMakeLists.txt|s3g Macro Shred 8ch"
  "plugins/clap_macro_shred/CMakeLists.txt|s3g Macro Shred 24ch"
  "plugins/clap_macro_fracture/CMakeLists.txt|s3g Macro Fracture Mono"
  "plugins/clap_macro_fracture/CMakeLists.txt|s3g Macro Fracture 8ch"
  "plugins/clap_macro_fracture/CMakeLists.txt|s3g Macro Fracture 24ch"
)
for contract in "${macro_family_names[@]}"; do
  file="${contract%%|*}"
  expected_name="${contract#*|}"
  if ! rg -Fq "\"${expected_name}\"" "$file"; then
    warn "name" "$file" \
      "Macro-family host names must expose the meaningful variant '${expected_name}'."
  fi
done

macro_family_sources=(
  plugins/clap_macro_delay/s3g_macro_delay_clap.cpp
  plugins/clap_macro_pitch/s3g_macro_pitch_clap.cpp
  plugins/clap_macro_shred/s3g_macro_shred_clap.cpp
  plugins/clap_macro_fracture/s3g_macro_fracture_clap.cpp
)
for file in "${macro_family_sources[@]}"; do
  if ! rg -q 'drawMacroTitleBand' "$file" \
      || ! rg -q 'macroTitleBand' "$file" \
      || ! rg -q 'handleProcessorTitleClick' "$file"; then
    warn "title" "$file" \
      "Every Macro uses aligned PRESET, LOAD, SAVE, and far-right PK title controls."
  fi
  if ! rg -q 'ResponsiveViewport' "$file"; then
    warn "layout" "$file" \
      "Every Macro editor uses the shared responsive viewport."
  fi
  if ! rg -q 'sliderDoubleClickDefault' "$file"; then
    warn "control" "$file" \
      "Every Macro slider must support double-click default reset."
  fi
  if ! rg -q 'drawProcessorSlider' "$file"; then
    warn "geometry" "$file" \
      "Macro sliders use shared left-label, track, and bounded-value anchors."
  fi
  if ! rg -q '@\"OUTPUT\"' "$file" \
      || ! rg -q '@\"OUT\"' "$file"; then
    warn "layout" "$file" \
      "Every Macro control stack begins with OUTPUT and OUT."
  fi
  if ! rg -q 'kMacro(FamilyLayout|ShredFamilyLayout)' "$file"; then
    warn "layout" "$file" \
      "Every Macro editor consumes an explicit shared family layout."
  fi
  if rg -q 'drawEncoderTitleBand|@\"RANDOM\"' "$file"; then
    warn "title" "$file" \
      "Macro title actions are PRESET / LOAD / SAVE only; RANDOM belongs to Encoders."
  fi
done
if ! rg -Fq 'kMacroFirstColumn { 18.0, 352.0, 42.0 }' \
    plugins/common/s3g_gui_layout.h \
    || ! rg -Fq 'kMacroSecondColumn { 388.0, 354.0, 42.0 }' \
    plugins/common/s3g_gui_layout.h \
    || ! rg -q 'PanelRole::LaneRelationships' \
    plugins/common/s3g_gui_layout.h \
    || ! rg -q 'processorSliderFitsPanel' \
    plugins/common/s3g_gui_layout.h \
    tests/gui_layout_contract_smoke.cpp; then
  warn "layout" "plugins/common/s3g_gui_layout.h" \
    "All multichannel Macros must share the 760 px two-column grid, keep lane relationships in the second column, and prove bounded slider-value geometry."
fi
macro_multichannel_title_contracts=(
  'plugins/clap_macro_delay/s3g_macro_delay_clap.cpp|s3g MACRO DELAY %uCH'
  'plugins/clap_macro_pitch/s3g_macro_pitch_clap.cpp|s3g MACRO PITCH %uCH'
  'plugins/clap_macro_shred/s3g_macro_shred_clap.cpp|s3g MACRO SHRED %uCH'
)
for contract in "${macro_multichannel_title_contracts[@]}"; do
  file="${contract%%|*}"
  title="${contract#*|}"
  if ! rg -Fq "$title" "$file" \
      || ! rg -q 'peakDbText' "$file"; then
    warn "title" "$file" \
      "Multichannel Macro titles must contain their 8CH/24CH suffix on the left and reserve the far-right status for PK."
  fi
done
if rg -q 'channelX|peakX|drawAtPoint.*%uCH' \
    plugins/clap_macro_shred/s3g_macro_shred_clap.cpp; then
  warn "title" "plugins/clap_macro_shred/s3g_macro_shred_clap.cpp" \
    "Macro Shred must not draw a separate channel-count label beside PK."
fi
if ! rg -q 'kMacroShredMonoFamilyLayout' \
    plugins/common/s3g_gui_layout.h \
    || ! rg -Fq '{ 18.0, 68.0, 380.0, 80.0 }' \
    plugins/common/s3g_gui_layout.h; then
  warn "layout" "plugins/common/s3g_gui_layout.h" \
    "Compact Shred Mono must retain its declared two-tier header and y 68 OUTPUT exception."
fi

section "Panner Family"
panner_family=(
  "plugins/clap_layout_panner/s3g_layout_panner_clap.cpp|s3g Panner Layout"
  "plugins/clap_dbap_panner/s3g_dbap_panner_clap.cpp|s3g Panner DBAP"
  "plugins/clap_lbap_panner/s3g_lbap_panner_clap.cpp|s3g Panner LBAP"
  "plugins/clap_vbap_panner/s3g_vbap_panner_clap.cpp|s3g Panner VBAP"
)
for contract in "${panner_family[@]}"; do
  file="${contract%%|*}"
  expected_name="${contract#*|}"
  if ! rg -Fq "\"${expected_name}\"" "$file"; then
    warn "name" "$file" "Panner-family host and title names must expose '${expected_name}'."
  fi
  if ! rg -q 'ResponsiveViewport' "$file" \
      || ! rg -q 'kPannerFamilyLayout' "$file"; then
    warn "layout" "$file" "Every Panner uses the shared responsive 900 x 720 family layout."
  fi
  if ! rg -q 'drawDecoderTitleBand' "$file" \
      || ! rg -q 'handleProcessorTitleClick' "$file"; then
    warn "title" "$file" "Panners share aligned PRESET, LOAD, SAVE, and far-right PK title controls."
  fi
  if ! rg -q 'drawPanelHeader\(@"OUTPUT"' "$file" \
      || ! rg -q 'drawSlider:@"OUT"' "$file"; then
    warn "layout" "$file" "A dedicated OUTPUT toolbox with OUT first must remain visible on every Panner page."
  fi
  if ! rg -q '646, 738' "$file"; then
    warn "geometry" "$file" "Panner labels and controls must retain the shared 16 px and 108 px panel insets."
  fi
  if ! rg -q 'sliderDoubleClickDefault' "$file"; then
    warn "control" "$file" "Every Panner slider, including mixer mirrors, must support double-click default reset."
  fi
  if ! rg -q '@"FIELD", @"MIXER", @"DESIGN"' "$file"; then
    warn "layout" "$file" "Every Panner exposes the common FIELD, MIXER, and DESIGN page order."
  fi
  if rg -q '16x64|16CH|64CH' "$file"; then
    warn "title" "$file" "Panner titles and PK status must not repeat non-distinguishing channel counts."
  fi
done

for decoder_title in \
    plugins/clap_ambisonic_head_decoder/s3g_ambisonic_head_decoder_clap.cpp \
    plugins/clap_ambisonic_stereo_decoder/s3g_ambisonic_stereo_decoder_clap.cpp; do
  if rg -q '2OUT|2CH' "$decoder_title"; then
    warn "title" "$decoder_title" \
      "Head and Stereo Decoder status must reserve the far-right position for PK."
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

sample_family_names=(
  'plugins/clap_sample_player/s3g_sample_player_clap.cpp|s3g Sample Player 2'
  'plugins/clap_sample_player/s3g_sample_player_clap.cpp|s3g Sample Player 16'
  'plugins/clap_breakbeat_slicer/s3g_breakbeat_slicer_clap.cpp|s3g Sample Slicer 2'
  'plugins/clap_breakbeat_slicer/s3g_breakbeat_slicer_clap.cpp|s3g Sample Slicer 16'
  'plugins/clap_sample_doubles/s3g_sample_doubles_clap.cpp|s3g Sample Doubles 2'
)
for contract in "${sample_family_names[@]}"; do
  file="${contract%%|*}"
  expected_name="${contract#*|}"
  if ! rg -Fq "\"${expected_name}\"" "$file"; then
    warn "name" "$file" \
      "Sample-family host names must expose the fixed channel count in '${expected_name}'."
  fi
done

processor_family_names=(
  'plugins/clap_delay_processor/CMakeLists.txt|s3g Processor Delay 8ch'
  'plugins/clap_delay_processor/CMakeLists.txt|s3g Processor Delay 24ch'
  'plugins/clap_buffer_processor/s3g_buffer_processor_clap.cpp|s3g Processor Buffer 8ch'
  'plugins/clap_wave_geometry_processor/CMakeLists.txt|s3g Processor Wave Geometry 8ch'
  'plugins/clap_loop_processor/s3g_loop_processor_clap.cpp|s3g Processor Loop 8ch'
  'plugins/clap_multi_loop_processor/s3g_multi_loop_processor_clap.cpp|s3g Processor Multi Loop 8ch'
  'plugins/clap_psd_raw_field/s3g_psd_raw_field_clap.cpp|s3g Processor Fault 8ch'
  'plugins/clap_no_input_mixer/s3g_no_input_mixer_clap.cpp|s3g Processor No Input Mixer 8ch'
  'plugins/clap_ambi_grain_processor/s3g_ambi_grain_processor_clap.cpp|s3g Processor Ambi Grain 16ch'
  'plugins/clap_spectral_topology_processor/CMakeLists.txt|s3g Processor Spectral 8ch'
  'plugins/clap_spectral_topology_processor/CMakeLists.txt|s3g Processor Spectral 24ch'
  'plugins/clap_feedback_shift/s3g_feedback_shift_clap.cpp|s3g Processor Feedback Shift'
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
  plugins/clap_no_input_mixer/s3g_no_input_mixer_clap.cpp
  plugins/clap_ambi_grain_processor/s3g_ambi_grain_processor_clap.cpp
  plugins/clap_spectral_topology_processor/s3g_spectral_topology_processor_clap.cpp
  plugins/clap_feedback_shift/s3g_feedback_shift_gui.inc
)
if ! rg -q 'drawDecoderTitleBand\(title, preset, status, band,[[:space:]]*$' \
    plugins/common/s3g_cocoa_gui.h \
    || ! rg -q 'softTitleAttrs\(\), softLabelAttrs\(\), softValueAttrs\(\), style' \
    plugins/common/s3g_cocoa_gui.h; then
  warn "typography" "plugins/common/s3g_cocoa_gui.h" \
    "Processor title typography must be fixed by the shared renderer rather than inherited from local palettes."
fi
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
  if ! rg -q '@"[^"]*OUTPUT[^"]*"' "$file" || ! rg -Fq '@"OUT"' "$file"; then
    warn "family" "$file" "Processor control stacks begin with OUTPUT and OUT."
  fi
  if ! rg -q 'PROCESSOR [^"]+([0-9]+|%u)CH' "$file"; then
    warn "name" "$file" "Processor GUI titles include the meaningful channel count."
  fi
  if rg -q '· [0-9]+(CH|OUT)|titleStatus[^\n]*(CH|OUT)' "$file"; then
    warn "layout" "$file" "Processor title status reserves the far-right edge for PK; channel count belongs in the full title."
  fi
  if rg -q 'drawDisclosurePanelHeader' "$file"; then
    warn "layout" "$file" \
      "Processor parameter toolboxes remain visible; use the responsive second-column contract instead of disclosure headers."
  fi
done

topology_processor_sources=(
  plugins/clap_delay_processor/s3g_delay_processor_clap.cpp
  plugins/clap_wave_geometry_processor/s3g_wave_geometry_processor_clap.cpp
  plugins/clap_spectral_topology_processor/s3g_spectral_topology_processor_clap.cpp
)
for file in "${topology_processor_sources[@]}"; do
  if ! rg -q 'kTopologyProcessorColumns' "$file" \
      || ! rg -q '(kDelayGuiTopologyPanelX|kSecondaryPanelX)' "$file"; then
    warn "layout" "$file" \
      "Topology Processors use the shared 1356 px two-column canvas with TOPOLOGY anchored in the second column."
  fi
  if ! rg -q '(kDelayGuiWidth\), 360u|kGuiWidth, 360u)' "$file"; then
    warn "layout" "$file" \
      "Topology Processor viewports advertise the full 1356 px canvas as their minimum width; never magnify or reduce the GUI."
  fi
  if ! rg -q 'kTopologyProcessorColumns\.field' "$file" \
      || ! rg -q 'fieldLayout\.height' "$file"; then
    warn "layout" "$file" \
      "Topology Processor field draw and hit geometry use the shared fixed-height field frame; never stretch the shell to the canvas bottom."
  fi
  if ! rg -q 'drawTopologyProcessorCameraButtons' "$file" \
      || ! rg -q 'topologyProcessorCameraButtonRect' "$file" \
      || ! rg -q 'topologyProcessorFieldContentRect' "$file"; then
    warn "layout" "$file" \
      "Topology Processor fields share TOP, SIDE, and 3/4 buttons plus direct click-drag camera geometry."
  fi
  if ! rg -q 'topologyProcessorChannelGrid' "$file" \
      || ! rg -q 'topologyProcessorChannelRect' "$file"; then
    warn "layout" "$file" \
      "Topology Processor second pages use the shared 600 px channel-view grid."
  fi
  if rg -q 'NSEventModifierFlagShift|peakDbText\(pk\)|NSMaxX\(rect\) - 92\.0' "$file"; then
    warn "layout" "$file" \
      "Topology camera drag needs no modifier, and PK appears only in the plugin title band."
  fi
done

delay_processor_source="plugins/clap_delay_processor/s3g_delay_processor_clap.cpp"
if ! rg -q 'topologyPanel\.origin\.x \+ 12\.0' "$delay_processor_source" \
    || ! rg -q 'sectionAttrs =.*softLabelAttrs|softLabelAttrs\(\);' "$delay_processor_source"; then
  warn "typography" "$delay_processor_source" \
    "Processor Delay uses the shared soft typography palette and 12 px topology-field title inset."
fi

fault_source="plugins/clap_psd_raw_field/s3g_psd_raw_field_clap.cpp"
if ! rg -q 'processorLabelX\(kLeftToolboxX\), kPerformanceRowY - 5\.0' "$fault_source" \
    || ! rg -q 'kStandardMetrics\.headerLabelInset, y \+ 7\.0' "$fault_source"; then
  warn "layout" "$fault_source" \
    "Processor Fault contextual labels and field headings must use the shared 16 px label and 8 px header anchors."
fi

section "Sample Family"
sample_player_source=plugins/clap_sample_player/s3g_sample_player_clap.cpp
sample_doubles_source=plugins/clap_sample_doubles/s3g_sample_doubles_clap.cpp
for contract in \
  drawProcessorTitleBand \
  handleProcessorTitleClick \
  drawPanelHeader \
  drawProcessorSlider \
  drawProcessorMenu \
  sliderDoubleClickDefault \
  ResponsiveViewport \
  peakDbText; do
  if ! rg -Fq "$contract" "$sample_player_source"; then
    warn "family" "$sample_player_source" \
      "Sample Player must use the shared ${contract} GUI convention."
  fi
done
if ! rg -q 'kStandardMetrics\.contentTop' "$sample_player_source" \
    || ! rg -Fq '"OUT"' "$sample_player_source"; then
  warn "layout" "$sample_player_source" \
    "Sample Player uses the shared content top and begins its OUTPUT controls with OUT."
fi
if ! rg -Fq '{ kGainParamId, "OUT", 18.0, 494.0, 342.0 }' \
    "$sample_doubles_source" \
    || ! rg -Fq '@"OUTPUT / DECKS / SOURCE"' "$sample_doubles_source"; then
  warn "layout" "$sample_doubles_source" \
    "Sample Doubles must begin its left control column with OUT under an OUTPUT-first panel header."
fi
if rg -q 'NSSlider|NSButton' "$sample_player_source"; then
  warn "control" "$sample_player_source" \
    "Sample Player uses the shared custom canvas controls instead of native Cocoa sliders or buttons."
fi
if ! rg -q 'defaultSafeSampleBounds|applySafeDefaultBounds' \
    "$sample_player_source" dsp/s3g_sample_asset.h; then
  warn "control" "$sample_player_source" \
    "Sample Player default Start, End, and loop boundaries must be sample-aware safe zero crossings."
fi
if ! rg -Uq 'attackProportion[^}]+decayProportion[^}]+releaseProportion' \
    dsp/s3g_sample_player.h \
    || ! rg -Fq 'migrateLegacyEnvelope' "$sample_player_source"; then
  warn "control" "$sample_player_source" \
    "Sample Player A/D/R must use Slicer-style proportions and migrate legacy millisecond state."
fi
if ! rg -q 'NSTrackingMouseMoved' "$sample_player_source" \
    || ! rg -Uq 'dropdownHitIndex\([^;]*playModeDropdownRect' "$sample_player_source" \
    || ! rg -Uq 'dropdownHitIndex\([^;]*filterTypeDropdownRect' "$sample_player_source" \
    || ! rg -Uq 'dropdownHitIndex\([^;]*pitchModeDropdownRect' "$sample_player_source" \
    || ! rg -Uq 'dropdownHitIndex\([^;]*triggerModeDropdownRect' "$sample_player_source" \
    || ! rg -Uq 'dropdownHitIndex\([^;]*retriggerModeDropdownRect' "$sample_player_source" \
    || ! rg -Uq 'dropdownHitIndex\([^;]*syncModeDropdownRect' "$sample_player_source" \
    || ! rg -Uq 'dropdownHitIndex\([^;]*voiceModeDropdownRect' "$sample_player_source" \
    || ! rg -Uq 'dropdownHitIndex\([^;]*midiReceiveDropdownRect' "$sample_player_source" \
    || ! rg -Fq '_playModeMenuHover' "$sample_player_source" \
    || ! rg -Fq '_filterTypeMenuHover' "$sample_player_source" \
    || ! rg -Fq '_pitchModeMenuHover' "$sample_player_source" \
    || ! rg -Fq '_triggerModeMenuHover' "$sample_player_source" \
    || ! rg -Fq '_retriggerModeMenuHover' "$sample_player_source" \
    || ! rg -Fq '_syncModeMenuHover' "$sample_player_source" \
    || ! rg -Fq '_voiceModeMenuHover' "$sample_player_source" \
    || ! rg -Fq '_midiReceiveMenuHover' "$sample_player_source"; then
  warn "control" "$sample_player_source" \
    "Sample Player menus must track, draw, and clear the pointer-hover row."
fi
if ! rg -Fq 'SyncMode::Host' dsp/s3g_sample_player.h \
    || ! rg -Fq 'TriggerMode::Auto' dsp/s3g_sample_player.h \
    || ! rg -Fq 'RetriggerMode::Layer' dsp/s3g_sample_player.h \
    || ! rg -Fq 'VoiceMode::Poly' dsp/s3g_sample_player.h \
    || ! rg -Fq 'CLAP_TRANSPORT_HAS_TEMPO' "$sample_player_source"; then
  warn "control" "$sample_player_source" \
    "Sample Player must retain compatibility-default host sync, trigger/retrigger, Poly, and transport behavior."
fi
if ! rg -Fq 'midiReceiveMenuRect' "$sample_player_source" \
    || ! rg -Fq 'receivesNoteOn' "$sample_player_source" \
    || ! rg -Fq 'outputPanelRect' "$sample_player_source" \
    || ! rg -Fq '@"OUTPUT / MIDI"' "$sample_player_source" \
    || rg -Fq 'midiPanelRect' "$sample_player_source" \
    || ! rg -Fq '{ kGainParamId, "OUT", 486.0' "$sample_player_source"; then
  warn "layout" "$sample_player_source" \
    "Sample Player must combine MIDI receive with its upper-right OUTPUT panel and begin with OUT."
fi
if ! rg -Fq 'applyFinalOutput(settings, outputs' dsp/s3g_sample_player.h \
    || ! rg -Fq 'voice.velocityLevel = velocity' dsp/s3g_sample_player.h \
    || rg -q 'voice\.(leftPan|rightPan)|voice\.level = .*gainDecibels' \
      dsp/s3g_sample_player.h; then
  warn "control" "$sample_player_source" \
    "Sample Player Out and stereo Pan must process the summed final output instead of being captured per note."
fi
if ! rg -Fq 'updateLivePitchTarget(voice, settings)' dsp/s3g_sample_player.h \
    || ! rg -Fq 'updateLiveEnvelopeTargets(voice, settings)' \
      dsp/s3g_sample_player.h \
    || ! rg -Fq 'updateLiveLoopGeometry(voice, settings)' \
      dsp/s3g_sample_player.h \
    || ! rg -Fq 'kLivePitchSmoothingSeconds = 0.010' \
      dsp/s3g_sample_player.h \
    || ! rg -Fq 'kLiveLoopTransitionSeconds = 0.005' \
      dsp/s3g_sample_player.h; then
  warn "control" "$sample_player_source" \
    "Sample Player Tune/Fine, Sustain/pre-release Release, and loop edits must update active voices through bounded smoothing."
fi
if ! rg -Fq 'PitchMode::Stretch' dsp/s3g_sample_player.h \
    || ! rg -Fq 'PitchMode::RateBelowStretchAbove' \
      dsp/s3g_sample_player.h \
    || ! rg -Fq '@"RATE BELOW / STRETCH ABOVE"' \
      "$sample_player_source" \
    || ! rg -Fq 'stretchSample' dsp/s3g_sample_player.h \
    || ! rg -Fq 'stretchPhaseStep' dsp/s3g_sample_player.h; then
  warn "control" "$sample_player_source" \
    "Sample Player Stretch and root-split Rate/Stretch modes must preserve transport timing while shifting pitch."
fi
if ! rg -Fq 'loopCrossfadeFrames' dsp/s3g_sample_player.h \
    || ! rg -Fq 'ForwardPingPong' dsp/s3g_sample_player.h \
    || ! rg -Fq 'processFilter' dsp/s3g_sample_player.h; then
  warn "control" "$sample_player_source" \
    "Sample Player must retain crossfaded wraps, ping-pong loops, and its multimode filter."
fi
if ! rg -Fq 'waveBoundaryAtPoint' "$sample_player_source" \
    || ! rg -Fq 'scrollWheel:' "$sample_player_source" \
    || ! rg -Fq 'waveVisibleSpan' "$sample_player_source"; then
  warn "control" "$sample_player_source" \
    "Sample Player waveform boundaries must remain directly draggable and zoomable."
fi
if ! rg -Fq 'voiceCursorCount' dsp/s3g_sample_player.h \
    || ! rg -Fq 'voiceCursorPositions' "$sample_player_source" \
    || ! rg -Fq 'voiceCursorKeys' "$sample_player_source"; then
  warn "control" "$sample_player_source" \
    "Sample Player must publish and draw one labeled cursor per active voice."
fi
if ! rg -Fq 'void killAll() noexcept' dsp/s3g_sample_player.h \
    || ! rg -Fq 'killAllButtonRect' "$sample_player_source" \
    || ! rg -Fq '@"KILL ALL"' "$sample_player_source" \
    || ! rg -Fq 'killRequested.exchange' "$sample_player_source" \
    || ! rg -Fq 'instance.engine.killAll()' "$sample_player_source"; then
  warn "control" "$sample_player_source" \
    "Sample Player must provide an audio-thread-safe Kill All action that clears every active voice."
fi
if ! rg -Fq 'paramIsExposed' "$sample_player_source" \
    || ! rg -Fq 'outputChannelCount_ == 2u' dsp/s3g_sample_player.h \
    || ! rg -Fq 'SOURCE CHANNEL RELATIONSHIPS PRESERVED' \
        "$sample_player_source"; then
  warn "control" "$sample_player_source" \
    "Sample Player 16 must hide Pan and preserve source-to-output lane mapping."
fi

section "Compact Effect Family"
compact_effect_sources=(
  plugins/clap_spectral_spray/s3g_spectral_spray_clap.cpp
  plugins/clap_8ch_spectral_spray/s3g_8ch_spectral_spray_clap.cpp
  plugins/clap_shard_scatter/s3g_shard_scatter_clap.cpp
  plugins/clap_orbit_delay/s3g_orbit_delay_clap.cpp
  plugins/clap_cascade_taps/s3g_cascade_taps_clap.cpp
)
for file in "${compact_effect_sources[@]}"; do
  if ! rg -q 'kCompactEffectFamilyLayout' "$file" \
      || ! rg -q 'drawCompactEffectTitleBand' "$file" \
      || ! rg -q 'ResponsiveViewport' "$file"; then
    warn "family" "$file" \
      "Compact Effects must consume the shared 760 x 376 layout, title band, and responsive viewport."
  fi
  if ! rg -q 'compactEffectOutputPanel' "$file" \
      || ! rg -q '@\"OUTPUT\"' "$file" \
      || ! rg -q '@\"OUT\"' "$file"; then
    warn "layout" "$file" \
      "Compact Effect control stacks begin with the shared OUTPUT panel and OUT."
  fi
  if ! rg -q 'sliderDoubleClickDefault' "$file" \
      || ! rg -q 'drawProcessorSlider' "$file"; then
    warn "control" "$file" \
      "Compact Effect sliders use bounded shared geometry and double-click defaults."
  fi
done
compact_effect_names=(
  'plugins/clap_spectral_spray/CMakeLists.txt|s3g Effect Spectral Spray 2ch'
  'plugins/clap_8ch_spectral_spray/CMakeLists.txt|s3g Effect Spectral Spray 8ch'
  'plugins/clap_shard_scatter/CMakeLists.txt|s3g Effect Shard Scatter'
  'plugins/clap_orbit_delay/CMakeLists.txt|s3g Effect Orbit Delay'
  'plugins/clap_cascade_taps/CMakeLists.txt|s3g Effect Cascade Taps'
)
for contract in "${compact_effect_names[@]}"; do
  file="${contract%%|*}"
  expected_name="${contract#*|}"
  if ! rg -Fq "\"${expected_name}\"" "$file"; then
    warn "name" "$file" \
      "Compact Effect host names must expose '${expected_name}'."
  fi
done

section "Ambi Effect Family"
ambi_effect_sources=(
  plugins/clap_ambi_effect_dj_filter/s3g_ambi_effect_dj_filter_clap.cpp
  plugins/clap_ambi_effect_delay/s3g_ambi_effect_delay_clap.cpp
  plugins/clap_ambi_effect_pitch_gain/s3g_ambi_effect_pitch_gain_clap.cpp
  plugins/clap_ambi_effect_displacement/s3g_ambi_effect_displacement_clap.cpp
)
for file in "${ambi_effect_sources[@]}"; do
  if ! rg -q 'ResponsiveViewport' "$file" \
      || ! rg -q 'sliderDoubleClickDefault' "$file" \
      || ! rg -q 'SPHERE 24' "$file"; then
    warn "family" "$file" \
      "Ambi Effects use responsive editors, standard slider defaults, and the shared Sphere24 pickup body."
  fi
done
trace_gui="plugins/clap_ambi_effect_trace/s3g_ambi_effect_trace_gui.inc"
if ! rg -q 'drawAmbiEffectTitleBand' "$trace_gui" \
    || ! rg -q 'drawPanel:kOutputPanel title:@"OUTPUT"' "$trace_gui" \
    || ! rg -q 'drawPanel:kTopologyPanel title:@"TOPOLOGY"' "$trace_gui" \
    || ! rg -q 'drawPanel:kMaskPanel title:@"DIRECTIONAL WET MASK"' "$trace_gui" \
    || ! rg -q 'drawTopologyProcessorCameraButtons' "$trace_gui" \
    || ! rg -q 'drawDropdownMenu' "$trace_gui"; then
  warn "layout" "$trace_gui" \
    "Trace editors use the Ambi Effect title, fitted panel headers, shared field cameras, and real dropdown menus."
fi
if rg -q '\[@"(SPATIAL ROUTING|DIRECTIONAL WET MASK)" drawAtPoint' "$trace_gui"; then
  warn "header" "$trace_gui" \
    "Trace section names are panel headers, not free-floating captions."
fi
if ! rg -q 'kAmbiEffectFamilyLayout' \
    plugins/clap_ambi_effect_displacement/s3g_ambi_effect_displacement_clap.cpp \
    || ! rg -q 'drawAmbiEffectTitleBand' \
    plugins/clap_ambi_effect_displacement/s3g_ambi_effect_displacement_clap.cpp; then
  warn "layout" "plugins/clap_ambi_effect_displacement/s3g_ambi_effect_displacement_clap.cpp" \
    "Ambi Effect Displacement uses the shared listener-field layout and title."
fi
ambi_effect_names=(
  'plugins/clap_ambi_effect_dj_filter/CMakeLists.txt|s3g Ambi Effect DJ Filter 64'
  'plugins/clap_ambi_effect_delay/CMakeLists.txt|s3g Ambi Effect Delay 64'
  'plugins/clap_ambi_effect_pitch_gain/CMakeLists.txt|s3g Ambi Effect Pitch 64'
  'plugins/clap_ambi_effect_pitch_gain/CMakeLists.txt|s3g Ambi Effect Gain 64'
  'plugins/clap_ambi_effect_displacement/CMakeLists.txt|s3g Ambi Effect Displacement 64'
)
for contract in "${ambi_effect_names[@]}"; do
  file="${contract%%|*}"
  expected_name="${contract#*|}"
  if ! rg -Fq "\"${expected_name}\"" "$file"; then
    warn "name" "$file" \
      "Ambi Effect host names must expose '${expected_name}'."
  fi
done

section "Parameter Surface"
surface_members=(
  plugins/clap_ambi_stochastic_encoder/s3g_ambi_stochastic_encoder_clap.cpp
  plugins/clap_ambi_wrangler_encoder/s3g_ambi_wrangler_encoder_clap.cpp
)
for file in "${surface_members[@]}"; do
  if ! rg -q 'drawParameterSurfaceVoronoi' "$file"; then
    warn "surface" "$file" \
      "SURF views use the shared exact Snapshot Surface-style Voronoi renderer, not sampled tiles."
  fi
  if ! rg -q 'displayParams|stochasticSurfaceParams' "$file"; then
    warn "surface" "$file" \
      "PLAY must draw participating toolbox controls from the effective interpolated state."
  fi
  if ! rg -q 'surfacePopRect' "$file" \
      || ! rg -q 'openSurfacePopup' "$file" \
      || ! rg -q 'hideSurfacePopup' "$file" \
      || ! rg -q 'destroySurfacePopup' "$file"; then
    warn "surface" "$file" \
      "SURF POP must provide the shared attached panel and follow CLAP hide/destroy lifecycle."
  fi
done
if ! rg -q 'kParameterSurfaceMaxCells = 24u' dsp/s3g_parameter_surface.h; then
  warn "surface" "dsp/s3g_parameter_surface.h" \
    "The shared Parameter Surface capacity is 24 cells and retains eight-cell state migration."
fi

section "Analyzer Family"
analyzer_family=(
  'plugins/clap_multichannel_meter/s3g_multichannel_meter_clap.cpp|s3g Analyzer Meter 64ch'
  'plugins/clap_ambisonic_energy_visualizer/s3g_ambisonic_energy_visualizer_clap.cpp|s3g Analyzer Ambi Energy 64ch'
)
for member in "${analyzer_family[@]}"; do
  file="${member%%|*}"
  expected_name="${member#*|}"
  if ! rg -Fq "\"${expected_name}\"" "$file"; then
    warn "name" "$file" "Analyzer host name must expose '${expected_name}'."
  fi
  if ! rg -q 'analyzerToolbarRect' "$file" \
      || ! rg -q 'analyzerContentRect' "$file" \
      || ! rg -q 'drawAnalyzerTitleBand' "$file" \
      || ! rg -q 'handleProcessorTitleClick' "$file"; then
    warn "family" "$file" \
      "Analyzers share the y 42 toolbar, y 86 primary display, and PRESET/LOAD/SAVE title band."
  fi
  if rg -q 'drawSlider' "$file" \
      && ! rg -q 'sliderDoubleClickDefault' "$file"; then
    warn "control" "$file" \
      "Continuous Analyzer display controls reset to their CLAP default on double-click."
  fi
done

section "Output Utility Family"
output_utility_family=(
  'plugins/clap_mc_to_stereo_autogain/s3g_mc_to_stereo_autogain_clap.cpp|s3g Output Autogain Stereo'
  'plugins/clap_mc_to_quad_autogain/s3g_mc_to_quad_autogain_clap.cpp|s3g Output Autogain Quad'
  'plugins/clap_sub_crossover/s3g_sub_crossover_clap.cpp|s3g Output Crossover'
)
for member in "${output_utility_family[@]}"; do
  file="${member%%|*}"
  expected_name="${member#*|}"
  if ! rg -Fq "\"${expected_name}\"" "$file"; then
    warn "name" "$file" "Output Utility host name must expose '${expected_name}'."
  fi
  if ! rg -q 'kOutputUtilityFamilyLayout' "$file" \
      || ! rg -q 'drawOutputUtilityTitleBand' "$file" \
      || ! rg -q 'ResponsiveViewport' "$file"; then
    warn "family" "$file" \
      "Output Utilities share the 920 x 560 field, parameter column, title band, and responsive viewport."
  fi
  if ! rg -q '@\"OUTPUT\"' "$file" \
      || ! rg -q 'sliderDoubleClickDefault' "$file" \
      || ! rg -q 'drawProcessorSlider' "$file"; then
    warn "layout" "$file" \
      "Output Utilities keep final-audition controls first and use shared bounded sliders with double-click defaults."
  fi
done

section "Ambi Imprint"
imprint_source=plugins/clap_ambi_imprint/s3g_ambi_imprint_clap.cpp
if ! rg -Fq '"s3g Processor Ambi Imprint 64ch"' "$imprint_source"; then
  warn "name" "$imprint_source" \
    "Ambi Imprint belongs to the developed Processor family."
fi
if ! rg -q 'kImprintFamilyLayout' "$imprint_source" \
    || ! rg -q 'drawImprintTitleBand' "$imprint_source" \
    || ! rg -q 'ResponsiveViewport' "$imprint_source" \
    || ! rg -q 'sliderDoubleClickDefault' "$imprint_source"; then
  warn "family" "$imprint_source" \
    "Ambi Imprint must use its shared responsive field/column layout, title band, and double-click defaults."
fi
if ! rg -q 'kImprintOutputPanel' "$imprint_source" \
    || ! rg -q '@\"OUTPUT\"' "$imprint_source" \
    || ! rg -q '@\"OUT\"' "$imprint_source"; then
  warn "layout" "$imprint_source" \
    "Ambi Imprint begins with a dedicated OUTPUT panel containing OUT and BYP."
fi

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
