#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const outputDirectory = path.dirname(fileURLToPath(import.meta.url));
const checkOnly = process.argv.includes("--check");
const generatedAt = "2026-07-31T12:00:00.000Z";
const feedbackColors = Object.freeze({
  positive: Object.freeze({ name: "orange", red: 201, green: 94, blue: 59 }),
  negative: Object.freeze({ name: "cyan", red: 87, green: 191, blue: 196 }),
});

const quadrants = Object.freeze([
  {
    channel: 0,
    filename: "nim_bu16_ch1_upper_left.json",
    id: "5d6bf092-d62a-46f0-817e-d50a781ddf81",
    label: "Upper Left",
    destinations: "1-4",
    sources: "1-4",
  },
  {
    channel: 1,
    filename: "nim_bu16_ch2_upper_right.json",
    id: "fcd43b88-bf93-45c1-81b6-fea2aab07713",
    label: "Upper Right",
    destinations: "1-4",
    sources: "5-8",
  },
  {
    channel: 2,
    filename: "nim_bu16_ch3_lower_left.json",
    id: "6f4981ab-cd2d-44e1-8156-94a0d2d861da",
    label: "Lower Left",
    destinations: "5-8",
    sources: "1-4",
  },
  {
    channel: 3,
    filename: "nim_bu16_ch4_lower_right.json",
    id: "3684a563-80ec-4986-b6cf-f10d7c0c8806",
    label: "Lower Right",
    destinations: "5-8",
    sources: "5-8",
  },
]);

function elementSetup() {
  return "--[[@sbc]] self:bmo(-1) self:bmi(0) self:bma(127)"
    + "--[[@sglc]] self:glc(-1,{{0,0,0,1}}) self:glp(-1,0)";
}

function buttonAction(channel) {
  return "--[[@cb]] local m,v,n=self:bmo(),self:bva(),self:ind() "
    + "if self:bst()>0 then if m==-1 then if v<1 then v=1 end "
    + `self:gms(${channel},144,n,v) self:bmo(-2) self:bmi(0) self:bma(2047) `
    + "else v=((v-32)*65)//1024 if v<1 then v=1 elseif v>127 then v=127 end "
    + `self:gms(${channel},160,n,v) end else self:gms(${channel},128,n,0) `
    + "self:bmo(-1) self:bmi(0) self:bma(127) end";
}

function controlElement(index, quadrant) {
  return {
    controlElementNumber: index,
    events: [
      { event: 0, config: elementSetup() },
      { event: 3, config: buttonAction(quadrant.channel) },
      { event: 6, config: "--[[@cb]] --[[timer unused]]" },
    ],
  };
}

function matrixFeedbackSetup(channel) {
  const positive = feedbackColors.positive;
  const negative = feedbackColors.negative;
  return "--[[@cb]] self.midirx_cb=function(s,h,d) "
    + "local ch,cmd,n,v=d[1],d[2],d[3],d[4] "
    + `if ch==${channel} and cmd==160 and n>=0 and n<16 then `
    + "local p=0 if v>64 then p=((v-64)*255)//63 "
    + `led_color(n,1,${positive.red},${positive.green},${positive.blue}) elseif v<64 then `
    + `p=((64-v)*255)//64 led_color(n,1,${negative.red},${negative.green},${negative.blue}) end `
    + "led_value(n,1,p) end end";
}

function systemElement(quadrant) {
  return {
    controlElementNumber: 255,
    events: [
      { event: 0, config: matrixFeedbackSetup(quadrant.channel) },
      { event: 4, config: "--[[@cb]] gpl(gpn())" },
      { event: 5, config: "--[[@cb]] --[[handled by midirx_cb]]" },
      { event: 6, config: "--[[@cb]] --[[timer unused]]" },
    ],
  };
}

function profile(quadrant) {
  return {
    id: quadrant.id,
    modifiedAt: generatedAt,
    createdAt: generatedAt,
    name: `s3g NIM BU16 ${quadrant.channel + 1} - ${quadrant.label}`,
    description: `No Input Mixer matrix: MIDI channel ${quadrant.channel + 1}, notes 0-15, native strike Note On followed by polyphonic pressure. Signed feedback fades ${feedbackColors.positive.name} for positive and ${feedbackColors.negative.name} for negative. Destinations ${quadrant.destinations}; sources ${quadrant.sources}.`,
    type: "BU16",
    version: { major: "1", minor: "5", patch: "7" },
    configType: "profile",
    configs: [
      ...Array.from({ length: 16 }, (_, index) =>
        controlElement(index, quadrant)),
      systemElement(quadrant),
    ],
  };
}

function serializedProfile(quadrant) {
  return `${JSON.stringify(profile(quadrant), null, 2)}\n`;
}

let failed = false;
for (const quadrant of quadrants) {
  const outputPath = path.join(outputDirectory, quadrant.filename);
  const expected = serializedProfile(quadrant);
  if (checkOnly) {
    const actual = fs.existsSync(outputPath)
      ? fs.readFileSync(outputPath, "utf8")
      : "";
    if (actual !== expected) {
      console.error(`${quadrant.filename} is missing or out of date`);
      failed = true;
    }
  } else {
    fs.writeFileSync(outputPath, expected);
    console.log(`Wrote ${quadrant.filename}`);
  }
}

if (failed) process.exitCode = 1;
