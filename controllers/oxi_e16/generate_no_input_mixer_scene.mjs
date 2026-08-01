#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const kMidiChannel = 16;
const kUsb3Output = 5;
const kInheritChannel = 0;
const kInheritOutput = 12;
const kRawMaximum = 16383;

const colors = Object.freeze({
  network: 56,
  movement: 74,
  surface: 84,
  output: 93,
  tone: 84,
  auxA: 34,
  auxB: 72,
  level: 93,
  body: 45,
  eq: 83,
  tune: 8,
  matrixSelf: 48,
  matrixNext: 56,
  matrixFar: 66,
  action: 93,
});

const iconRows = [
  "................",
  "..##........##..",
  "..###......###..",
  "...###....###...",
  "....###..###....",
  ".....######.....",
  "......####......",
  "..############..",
  "..############..",
  "......####......",
  ".....######.....",
  "....###..###....",
  "...###....###...",
  "..###......###..",
  "..##........##..",
  "................",
];

function iconFromRows(rows) {
  if (rows.length !== 16 || rows.some((row) => row.length !== 16)) {
    throw new Error("The E16 icon must be exactly 16 by 16 pixels");
  }
  const bytes = [];
  for (const row of rows) {
    for (let half = 0; half < 2; ++half) {
      let byte = 0;
      for (let bit = 0; bit < 8; ++bit) {
        if (row[half * 8 + bit] === "#") byte |= 1 << (7 - bit);
      }
      bytes.push(byte);
    }
  }
  return bytes;
}

function disabledPush() {
  return {
    instrument: 127,
    parameter: 0,
    type: 0,
    display: 0,
    mode: 0,
    channel: kInheritChannel,
    lower: 0,
    upper: 127,
    nr1: 0,
    nr2: 0,
    output: kInheritOutput,
    scriptId: 0,
  };
}

function notePush(note, velocity = 127) {
  return {
    instrument: 127,
    parameter: 0,
    type: 1,
    display: 0,
    mode: 0,
    channel: kInheritChannel,
    lower: 0,
    upper: 127,
    nr1: note,
    nr2: velocity,
    output: kInheritOutput,
    scriptId: 0,
  };
}

function disabledTurn() {
  return {
    instrument: 127,
    parameter: 0,
    type: 0,
    display: 10,
    mode: 3,
    channel: kInheritChannel,
    lower: 0,
    upper: 127,
    defaultValue: 0,
    nr1: 0,
    nr2: 0,
    output: kInheritOutput,
    scriptId: 0,
  };
}

function nrpnTurn(parameterId, mode = 3) {
  return {
    instrument: 127,
    parameter: 0,
    type: 9,
    // App 1.5 normalizes imported NRPN controls to this display mode.
    display: 11,
    mode,
    channel: kInheritChannel,
    lower: 0,
    upper: kRawMaximum,
    defaultValue: 0,
    nr1: parameterId & 0x7f,
    nr2: (parameterId >> 7) & 0x7f,
    output: kInheritOutput,
    scriptId: 0,
  };
}

function encoder({
  name,
  abbr,
  id,
  secondId,
  color,
  bipolar = false,
  pushNote,
  mode = 3,
}) {
  return {
    name,
    abbr,
    color,
    push_action: pushNote === undefined
      ? disabledPush()
      : notePush(pushNote),
    turn_actions: [
      nrpnTurn(id, mode),
      secondId === undefined ? disabledTurn() : nrpnTurn(secondId, mode),
    ],
    // App 1.5 currently emits zero here, including for a second destination.
    color2: 0,
  };
}

function pushEncoder({ name, abbr, color, pushNote }) {
  return {
    name,
    abbr,
    color,
    push_action: notePush(pushNote),
    turn_actions: [disabledTurn(), disabledTurn()],
    color2: 0,
  };
}

function page(title, encoders) {
  return {
    title,
    output: kUsb3Output,
    channel: kMidiChannel,
    encoders,
  };
}

function laneId(lane, offset) {
  return 1000 + lane * 100 + offset;
}

function insertId(lane, slot, field) {
  return laneId(lane, 20 + slot * 10 + field);
}

function matrixId(destination, source) {
  return 100 + destination * 8 + source;
}

function lanes(offset, makeControl) {
  return Array.from({ length: 8 }, (_, lane) =>
    makeControl(lane, laneId(lane, offset)));
}

function mixPage() {
  return page("MIXER", [
    ...lanes(2, (lane, id) => encoder({
      name: `L${lane + 1} Level`, abbr: `${lane + 1}LVL`, id,
      color: colors.level, pushNote: 32 + lane,
    })),
    ...lanes(0, (lane, id) => encoder({
      name: `L${lane + 1} Body`, abbr: `${lane + 1}BDY`, id,
      color: colors.body,
    })),
  ]);
}

function twoLaneRowsPage(title, first, second) {
  return page(title, [
    ...lanes(first.offset, (lane, id) => encoder({
      name: `L${lane + 1} ${first.name}`,
      abbr: `${lane + 1}${first.abbr}`,
      id,
      color: first.color,
      bipolar: first.bipolar,
      pushNote: first.pushBase === undefined
        ? undefined : first.pushBase + lane,
    })),
    ...lanes(second.offset, (lane, id) => encoder({
      name: `L${lane + 1} ${second.name}`,
      abbr: `${lane + 1}${second.abbr}`,
      id,
      color: second.color,
      bipolar: second.bipolar,
      pushNote: second.pushBase === undefined
        ? undefined : second.pushBase + lane,
    })),
  ]);
}

const pages = [
  page("LIVE", [
    encoder({ name: "Feedback", abbr: "FDBK", id: 5, color: colors.network }),
    encoder({ name: "Coupling", abbr: "COUP", id: 6, color: colors.network }),
    encoder({ name: "Flow", abbr: "FLOW", id: 16, color: colors.movement }),
    encoder({ name: "Phase", abbr: "PHAS", id: 7, color: colors.network }),
    encoder({ name: "Agency", abbr: "AGCY", id: 11, color: colors.network }),
    encoder({ name: "Motion", abbr: "MOTN", id: 19, color: colors.movement }),
    encoder({ name: "Spread", abbr: "SPRD", id: 17, color: colors.movement }),
    encoder({ name: "Vortex", abbr: "VRTX", id: 18, color: colors.movement, bipolar: true }),
    encoder({ name: "Formant", abbr: "FORM", id: 9, color: colors.tone }),
    encoder({ name: "Space", abbr: "SPAC", id: 12, color: colors.network }),
    encoder({ name: "SurfaceX", abbr: "SRFX", id: 55, color: colors.surface }),
    encoder({ name: "SurfaceY", abbr: "SRFY", id: 56, color: colors.surface }),
    encoder({ name: "Aux A Rt", abbr: "ARET", id: 26, color: colors.auxA }),
    encoder({ name: "Aux B Rt", abbr: "BRET", id: 31, color: colors.auxB }),
    encoder({ name: "Drift", abbr: "DRFT", id: 8, color: colors.network }),
    encoder({ name: "Output", abbr: "OUT!", id: 1, color: colors.output, pushNote: 123 }),
  ]),
  page("MOTION", [
    encoder({ name: "Event Rt", abbr: "EVRT", id: 36, color: colors.movement }),
    encoder({ name: "Event Ln", abbr: "EVLN", id: 37, color: colors.movement }),
    encoder({ name: "Density", abbr: "DENS", id: 38, color: colors.movement }),
    encoder({ name: "Chaos", abbr: "CHAO", id: 39, color: colors.movement }),
    encoder({ name: "Slew", abbr: "SLEW", id: 40, color: colors.movement }),
    encoder({ name: "Choke", abbr: "CHOK", id: 41, color: colors.movement }),
    encoder({ name: "Move Rt", abbr: "MVRT", id: 21, color: colors.movement }),
    encoder({ name: "Move Ph", abbr: "MVPH", id: 22, color: colors.movement }),
    encoder({ name: "R Depth", abbr: "RDEP", id: 45, color: colors.surface }),
    encoder({ name: "R Thresh", abbr: "RTHR", id: 46, color: colors.surface }),
    encoder({ name: "R Attack", abbr: "RATK", id: 47, color: colors.surface }),
    encoder({ name: "R Relea", abbr: "RREL", id: 48, color: colors.surface }),
    encoder({ name: "R Polar", abbr: "RPOL", id: 49, color: colors.surface, bipolar: true }),
    encoder({ name: "Shape", abbr: "SHAP", id: 20, color: colors.movement }),
    encoder({ name: "Behavior", abbr: "BEHV", id: 35, color: colors.movement }),
    encoder({ name: "Output", abbr: "OUT!", id: 1, color: colors.output, pushNote: 123 }),
  ]),
  mixPage(),
  page("MATRIX1", [
    ...Array.from({ length: 8 }, (_, lane) => encoder({
      name: `M${lane + 1} Self`,
      abbr: `${lane + 1}SLF`,
      id: matrixId(lane, lane),
      color: colors.matrixSelf,
      bipolar: true,
    })),
    ...Array.from({ length: 8 }, (_, source) => {
      const destination = (source + 1) % 8;
      return encoder({
        name: source === 7 ? "M8>1 P" : `M${source + 1}>${destination + 1}`,
        abbr: source === 7 ? "8>1!" : `${source + 1}>${destination + 1}`,
        id: matrixId(destination, source),
        color: colors.matrixNext,
        bipolar: true,
        pushNote: source === 7 ? 123 : undefined,
      });
    }),
  ]),
  page("MATRIX2", [
    ...Array.from({ length: 8 }, (_, index) => {
      const source = (index + 1) % 8;
      const destination = index;
      return encoder({
        name: `M${source + 1}>${destination + 1}`,
        abbr: `${source + 1}>${destination + 1}`,
        id: matrixId(destination, source),
        color: colors.matrixNext,
        bipolar: true,
      });
    }),
    ...[
      [0, 4], [4, 0], [1, 5], [5, 1],
      [2, 6], [6, 2], [3, 7], [7, 3],
    ].map(([source, destination]) => encoder({
      name: `M${source + 1}>${destination + 1}`,
      abbr: `${source + 1}>${destination + 1}`,
      id: matrixId(destination, source),
      color: colors.matrixFar,
      bipolar: true,
    })),
  ]),
  page("SENDS", [
    ...lanes(8, (lane, id) => encoder({
      name: `A${lane + 1} Send`, abbr: `A${lane + 1}SD`, id,
      color: colors.auxA,
    })),
    ...lanes(9, (lane, id) => encoder({
      name: `B${lane + 1} Send`, abbr: `B${lane + 1}SD`, id,
      color: colors.auxB,
    })),
  ]),
  page("AUXTONE", [
    encoder({ name: "Int Tone", abbr: "INTR", id: 14, color: colors.tone, bipolar: true }),
    encoder({ name: "Hse Tone", abbr: "HOUS", id: 15, color: colors.tone, bipolar: true }),
    encoder({ name: "A Type", abbr: "ATYP", id: 23, color: colors.auxA }),
    encoder({ name: "B Type", abbr: "BTYP", id: 28, color: colors.auxB }),
    encoder({ name: "A Gain", abbr: "AGAN", id: 24, color: colors.auxA }),
    encoder({ name: "A Tone", abbr: "ATON", id: 25, color: colors.auxA }),
    encoder({ name: "A Bias", abbr: "ABIA", id: 42, color: colors.auxA, bipolar: true }),
    encoder({ name: "A Return", abbr: "AR/M", id: 26, color: colors.auxA, pushNote: 72 }),
    encoder({ name: "B Gain", abbr: "BGAN", id: 29, color: colors.auxB }),
    encoder({ name: "B Tone", abbr: "BTON", id: 30, color: colors.auxB }),
    encoder({ name: "B Bias", abbr: "BBIA", id: 43, color: colors.auxB, bipolar: true }),
    encoder({ name: "B Return", abbr: "BR/M", id: 31, color: colors.auxB, pushNote: 73 }),
    encoder({ name: "A Feed", abbr: "AFBK", id: 27, color: colors.auxA }),
    encoder({ name: "B Feed", abbr: "BFBK", id: 32, color: colors.auxB }),
    encoder({ name: "Ceiling", abbr: "CEIL", id: 2, color: colors.output }),
    encoder({ name: "Output", abbr: "OUT!", id: 1, color: colors.output, pushNote: 123 }),
  ]),
  twoLaneRowsPage("EQ LOHI", {
    offset: 4, name: "Low", abbr: "LOW", color: colors.eq,
    bipolar: true,
  }, {
    offset: 7, name: "High", abbr: "HIG", color: colors.eq,
    bipolar: true,
  }),
  twoLaneRowsPage("EQ MID", {
    offset: 5, name: "MidHz", abbr: "MHZ", color: colors.eq,
    bipolar: false,
  }, {
    offset: 6, name: "MidGn", abbr: "MGN", color: colors.eq,
    bipolar: true,
  }),
  twoLaneRowsPage("TUNING", {
    offset: 10, name: "Note", abbr: "NOT", color: colors.tune,
    bipolar: false, pushBase: 80,
  }, {
    offset: 11, name: "Cents", abbr: "CTS", color: colors.tune,
    bipolar: true,
  }),
  page("RETURNS", [
    ...lanes(15, (lane, id) => encoder({
      name: `A${lane + 1} Ret`, abbr: `A${lane + 1}RN`, id,
      color: colors.auxA, bipolar: true,
    })),
    ...lanes(16, (lane, id) => encoder({
      name: `B${lane + 1} Ret`, abbr: `B${lane + 1}RN`, id,
      color: colors.auxB, bipolar: true,
    })),
  ]),
  page("ACTIONS", [
    pushEncoder({ name: "Record", abbr: "REC", color: colors.action, pushNote: 112 }),
    pushEncoder({ name: "Playback", abbr: "PLAY", color: colors.action, pushNote: 113 }),
    pushEncoder({ name: "Clr Last", abbr: "CLRL", color: colors.action, pushNote: 114 }),
    pushEncoder({ name: "Clr All", abbr: "CLRA", color: colors.action, pushNote: 115 }),
    pushEncoder({ name: "Cancel", abbr: "CNCL", color: colors.action, pushNote: 116 }),
    pushEncoder({ name: "Mx Flip", abbr: "FLIP", color: colors.matrixNext, pushNote: 117 }),
    pushEncoder({ name: "Mx Latch", abbr: "LTCH", color: colors.matrixFar, pushNote: 118 }),
    pushEncoder({ name: "New Sign", abbr: "SIGN", color: colors.matrixFar, pushNote: 119 }),
    pushEncoder({ name: "Seed", abbr: "SEED", color: colors.action, pushNote: 120 }),
    pushEncoder({ name: "Rand Low", abbr: "RLO", color: colors.action, pushNote: 125 }),
    pushEncoder({ name: "Rand Mid", abbr: "RMD", color: colors.action, pushNote: 122 }),
    pushEncoder({ name: "Rand Hi", abbr: "RHI", color: colors.action, pushNote: 126 }),
    pushEncoder({ name: "Forget", abbr: "FORG", color: colors.action, pushNote: 121 }),
    pushEncoder({ name: "Clear Mx", abbr: "MX0", color: colors.action, pushNote: 124 }),
    encoder({ name: "BU16Ramp", abbr: "RAMP", id: 59, color: colors.movement }),
    encoder({ name: "Output", abbr: "OUT!", id: 1, color: colors.output, pushNote: 123 }),
  ]),
];

// Arrange the 4-by-3 page grid by application area: global/performance,
// routing, then per-lane shaping.
const pageLayoutOrder = [0, 1, 6, 11, 3, 4, 5, 10, 2, 7, 8, 9];
const orderedPages = pageLayoutOrder.map((index) => pages[index]);

// Keep the gesture transport under the same five encoder presses on every
// performance page, including ACTIONS.
const gesturePushNotes = [112, 113, 114, 115, 116];
for (const currentPage of orderedPages) {
  gesturePushNotes.forEach((note, encoderIndex) => {
    currentPage.encoders[encoderIndex].push_action = notePush(note);
  });
}

export const scene = {
  title: "NIM P2",
  icon: iconFromRows(iconRows),
  selectedPreset: 0,
  color: 0,
  transmitMode: 0,
  pcOnEntry: 0,
  bankOnEntry: 65535,
  recentPage: 0,
  recentPreset: 0,
  transmitOnSceneEntry: 0,
  transmitOnPageSwitch: 0,
  transmitOnPresetLoad: 0,
  smartTransmit: 15,
  outputOnEntry: 0,
  holdMode: 0,
  acceleration: 3,
  code: { code: "", fullScript: "", scriptName: "" },
  pages: orderedPages,
};

function validParameterId(id) {
  if (id >= 1 && id <= 59) return true;
  if (id >= 100 && id <= 163) return true;
  if (id < 1000 || id > 1799) return false;
  const lane = Math.floor((id - 1000) / 100);
  const offset = (id - 1000) % 100;
  if (lane < 0 || lane >= 8) return false;
  if (offset >= 0 && offset <= 16) return true;
  return [20, 21, 22, 23, 24, 25,
    30, 31, 32, 33, 34, 35,
    40, 41, 42, 43, 44, 45].includes(offset);
}

const categoricalGlobalIds = new Set([
  3, 4, 10, 20, 23, 28, 33, 34, 35, 44, 50, 51, 52, 53, 54, 57, 58,
]);

// These finite selectors are intentionally exposed as E16 turns. No Input
// Mixer rounds their normalized 14-bit NRPN values to the nearest choice and
// returns that choice to the encoder ring.
const e16TurnCategoricalIds = new Set([20, 23, 28, 35]);

function categoricalParameterId(id) {
  if (categoricalGlobalIds.has(id)) return true;
  if (id < 1000 || id > 1799) return false;
  const offset = (id - 1000) % 100;
  if ([3, 12, 13, 14].includes(offset)) return true;
  if (offset < 20 || offset > 45) return false;
  const insertField = offset % 10;
  return insertField === 0 || insertField === 5;
}

function parameterId(action) {
  return (action.nr2 << 7) | action.nr1;
}

function validCommandNote(note) {
  return (note >= 32 && note <= 73)
    || (note >= 80 && note <= 87)
    || (note >= 112 && note <= 119)
    || (note >= 120 && note <= 126);
}

const pushFields = [
  "instrument", "parameter", "type", "display", "mode", "channel",
  "lower", "upper", "nr1", "nr2", "output", "scriptId",
];
const turnFields = [
  "instrument", "parameter", "type", "display", "mode", "channel",
  "lower", "upper", "defaultValue", "nr1", "nr2", "output", "scriptId",
];
const encoderFields = [
  "name", "abbr", "color", "push_action", "turn_actions", "color2",
];
const sceneFields = [
  "title", "icon", "selectedPreset", "color", "transmitMode", "pcOnEntry",
  "bankOnEntry", "recentPage", "recentPreset", "transmitOnSceneEntry",
  "transmitOnPageSwitch", "transmitOnPresetLoad", "smartTransmit",
  "outputOnEntry", "holdMode", "acceleration", "code", "pages",
];

function sameFields(object, expected) {
  return JSON.stringify(Object.keys(object)) === JSON.stringify(expected);
}

export function validateScene(value) {
  const errors = [];
  if (!sameFields(value, sceneFields)) {
    errors.push("scene has an invalid App 1.5 field layout");
  }
  if (value.title.length > 7) errors.push("scene title exceeds 7 characters");
  if (!Array.isArray(value.icon) || value.icon.length !== 32) {
    errors.push("scene icon must contain 32 bytes");
  }
  if (!Array.isArray(value.pages) || value.pages.length !== 12) {
    errors.push("scene must contain exactly 12 pages");
    return errors;
  }
  if (value.transmitOnSceneEntry !== 0
      || value.transmitOnPageSwitch !== 0
      || value.transmitOnPresetLoad !== 0) {
    errors.push("automatic scene, page, and preset transmission must be disabled");
  }
  if (!value.code || value.code.code !== "" || value.code.fullScript !== ""
      || value.code.scriptName !== "") {
    errors.push("the first scene must not contain an E16 script");
  }

  value.pages.forEach((currentPage, pageIndex) => {
    const context = `page ${pageIndex + 1}`;
    if (currentPage.title.length > 7) {
      errors.push(`${context} title exceeds 7 characters`);
    }
    if (currentPage.output !== kUsb3Output) {
      errors.push(`${context} must route to USB3`);
    }
    if (currentPage.channel !== kMidiChannel) {
      errors.push(`${context} must use MIDI channel 16`);
    }
    if (!Array.isArray(currentPage.encoders)
        || currentPage.encoders.length !== 16) {
      errors.push(`${context} must contain exactly 16 encoders`);
      return;
    }

    currentPage.encoders.forEach((currentEncoder, encoderIndex) => {
      const control = `${context}, encoder ${encoderIndex + 1}`;
      if (!sameFields(currentEncoder, encoderFields)) {
        errors.push(`${control} has an invalid App 1.5 field layout`);
      }
      if (!currentEncoder.name || currentEncoder.name.length > 8) {
        errors.push(`${control} name must contain 1-8 characters`);
      }
      if (!currentEncoder.abbr || currentEncoder.abbr.length > 4) {
        errors.push(`${control} abbreviation must contain 1-4 characters`);
      }
      if (!sameFields(currentEncoder.push_action, pushFields)) {
        errors.push(`${control} push action has an invalid field layout`);
      }
      if (!Array.isArray(currentEncoder.turn_actions)
          || currentEncoder.turn_actions.length !== 2) {
        errors.push(`${control} must contain two turn actions`);
        return;
      }
      currentEncoder.turn_actions.forEach((action, actionIndex) => {
        if (!sameFields(action, turnFields)) {
          errors.push(`${control}, turn ${actionIndex + 1} has an invalid field layout`);
        }
        if (action.type === 9) {
          const id = parameterId(action);
          if (!validParameterId(id)) {
            errors.push(`${control} addresses invalid NRPN parameter ${id}`);
          }
          if (categoricalParameterId(id) && !e16TurnCategoricalIds.has(id)) {
            errors.push(`${control} assigns categorical parameter ${id} to a turn`);
          }
          if (action.lower !== 0 || action.upper !== kRawMaximum) {
            errors.push(`${control} NRPN range must be 0-16383`);
          }
        } else if (action.type !== 0) {
          errors.push(`${control} contains unexpected turn type ${action.type}`);
        }
      });
      if (currentEncoder.push_action.type === 1
          && !validCommandNote(currentEncoder.push_action.nr1)) {
        errors.push(`${control} uses unknown command note ${currentEncoder.push_action.nr1}`);
      } else if (![0, 1].includes(currentEncoder.push_action.type)) {
        errors.push(`${control} contains unexpected push type ${currentEncoder.push_action.type}`);
      }
    });
  });
  value.pages.forEach((currentPage, pageIndex) => {
    gesturePushNotes.forEach((expectedNote, encoderIndex) => {
      const push = currentPage.encoders[encoderIndex].push_action;
      if (push.type !== 1 || push.nr1 !== expectedNote) {
        errors.push(`page ${pageIndex + 1}, encoder ${encoderIndex + 1} must keep gesture command note ${expectedNote}`);
      }
    });
  });
  return errors;
}

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const explicitOutputPath = process.argv.find((argument) => argument.endsWith(".oxie16"));
const outputPaths = explicitOutputPath === undefined
  ? [
      path.join(scriptDirectory, "s3g_no_input_mixer.oxie16"),
      path.join(scriptDirectory, "Scenes", "NIM P2.oxie16"),
    ]
  : [explicitOutputPath];
const errors = validateScene(scene);
if (errors.length !== 0) {
  for (const error of errors) console.error(`error: ${error}`);
  process.exit(1);
}

const rendered = `${JSON.stringify(scene)}\n`;
if (process.argv.includes("--check")) {
  let stale = false;
  for (const outputPath of outputPaths) {
    if (!fs.existsSync(outputPath)
        || fs.readFileSync(outputPath, "utf8") !== rendered) {
      console.error(`error: generated scene is stale: ${outputPath}`);
      stale = true;
    } else {
      console.log(`OXI E16 scene valid: ${outputPath}`);
    }
  }
  if (stale) process.exit(1);
} else {
  for (const outputPath of outputPaths) {
    fs.mkdirSync(path.dirname(outputPath), { recursive: true });
    fs.writeFileSync(outputPath, rendered);
    console.log(`Wrote OXI E16 scene: ${outputPath}`);
  }
}
