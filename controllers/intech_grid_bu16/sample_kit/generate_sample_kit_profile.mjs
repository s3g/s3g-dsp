#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const outputDirectory = path.dirname(fileURLToPath(import.meta.url));
const outputFilename = "sample_kit_bu16_4x4.json";
const checkOnly = process.argv.includes("--check");
const generatedAt = "2026-09-23T20:00:00.000Z";

const midiChannel = 0; // Grid and MIDI status bytes use zero-based channels.
const idleColor = Object.freeze({ red: 119, green: 169, blue: 155 });
const hitColor = Object.freeze({ red: 218, green: 139, blue: 104 });
const idleBrightness = 48;
const hitBrightness = 255;
const velocityGainNumerator = 5;
const velocityGainDenominator = 2;

// BU16 elements run left-to-right, top-to-bottom. Sample Kit presents Pad 1
// at bottom-left, so reverse the physical row order while preserving columns.
const notes = Object.freeze([
  48, 49, 50, 51,
  44, 45, 46, 47,
  40, 41, 42, 43,
  36, 37, 38, 39,
]);

function colorCommand(color) {
  return `self:glc(1,{{${color.red},${color.green},${color.blue},1}})`;
}

function elementSetup() {
  return "--[[@sbc]] self:bmo(-1) self:bmi(0) self:bma(127)"
    + `--[[@sglc]] ${colorCommand(idleColor)} `
    + `self:glp(1,${idleBrightness})`;
}

function buttonAction(note) {
  return "--[[@cb]] local v=self:bva() if self:bst()>0 then "
    + "if v<1 then v=1 end "
    + `v=(v*${velocityGainNumerator}+1)//${velocityGainDenominator} `
    + "if v>127 then v=127 end "
    + `self:gms(${midiChannel},144,${note},v) `
    + `${colorCommand(hitColor)} self:glp(1,${hitBrightness}) `
    + `else self:gms(${midiChannel},128,${note},0) `
    + `${colorCommand(idleColor)} self:glp(1,${idleBrightness}) end`;
}

function controlElement(index) {
  return {
    controlElementNumber: index,
    events: [
      { event: 0, config: elementSetup() },
      { event: 3, config: buttonAction(notes[index]) },
      { event: 6, config: "--[[@cb]] --[[timer unused]]" },
    ],
  };
}

function systemElement() {
  return {
    controlElementNumber: 255,
    events: [
      { event: 0, config: "--[[@cb]] --[[no host feedback required]]" },
      { event: 4, config: "--[[@cb]] gpl(gpn())" },
      { event: 5, config: "--[[@cb]] --[[MIDI receive unused]]" },
      { event: 6, config: "--[[@cb]] --[[timer unused]]" },
    ],
  };
}

function profile() {
  return {
    id: "08a37c24-1770-4f0e-958f-993920557da5",
    modifiedAt: generatedAt,
    createdAt: generatedAt,
    name: "s3g Sample Kit BU16 4x4",
    description: "One velocity-sensitive BU16 mapped to Sample Kit Pads 1-16 on MIDI channel 1 and notes 36-51. Native strike velocity is expanded 2.5x and clipped at 127. Physical rows match the plugin: Pads 1-4 at the bottom and Pads 13-16 at the top. Pads glow teal at rest and orange while held.",
    type: "BU16",
    version: { major: "1", minor: "5", patch: "7" },
    configType: "profile",
    configs: [
      ...Array.from({ length: notes.length }, (_, index) =>
        controlElement(index)),
      systemElement(),
    ],
  };
}

function serializedProfile() {
  return `${JSON.stringify(profile(), null, 2)}\n`;
}

const outputPath = path.join(outputDirectory, outputFilename);
const expected = serializedProfile();
if (checkOnly) {
  const actual = fs.existsSync(outputPath)
    ? fs.readFileSync(outputPath, "utf8")
    : "";
  if (actual !== expected) {
    console.error(`${outputFilename} is missing or out of date`);
    process.exitCode = 1;
  }
} else {
  fs.writeFileSync(outputPath, expected);
  console.log(`Wrote ${outputFilename}`);
}
