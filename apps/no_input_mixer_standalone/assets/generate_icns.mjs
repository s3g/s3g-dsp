#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const assetDirectory = path.dirname(fileURLToPath(import.meta.url));
const iconsetDirectory = path.join(
  assetDirectory, "AppIcon.xcassets", "AppIcon.appiconset");
const outputPath = path.join(assetDirectory, "no_input_mixer.icns");

const entries = [
  ["icp4", "icon_16x16.png", 16],
  ["ic11", "icon_16x16@2x.png", 32],
  ["icp5", "icon_32x32.png", 32],
  ["ic12", "icon_32x32@2x.png", 64],
  ["ic07", "icon_128x128.png", 128],
  ["ic13", "icon_128x128@2x.png", 256],
  ["ic08", "icon_256x256.png", 256],
  ["ic14", "icon_256x256@2x.png", 512],
  ["ic09", "icon_512x512.png", 512],
  ["ic10", "icon_512x512@2x.png", 1024],
];

function readPng(filename, expectedSize) {
  const png = fs.readFileSync(path.join(iconsetDirectory, filename));
  const signature = "89504e470d0a1a0a";
  if (png.subarray(0, 8).toString("hex") !== signature) {
    throw new Error(`${filename} is not a PNG`);
  }
  const width = png.readUInt32BE(16);
  const height = png.readUInt32BE(20);
  if (width !== expectedSize || height !== expectedSize) {
    throw new Error(
      `${filename} is ${width}x${height}; expected ${expectedSize}x${expectedSize}`);
  }
  return png;
}

const chunks = entries.map(([type, filename, expectedSize]) => {
  const png = readPng(filename, expectedSize);
  const chunk = Buffer.allocUnsafe(8 + png.length);
  chunk.write(type, 0, 4, "ascii");
  chunk.writeUInt32BE(chunk.length, 4);
  png.copy(chunk, 8);
  return chunk;
});

const totalLength = 8 + chunks.reduce((sum, chunk) => sum + chunk.length, 0);
const header = Buffer.allocUnsafe(8);
header.write("icns", 0, 4, "ascii");
header.writeUInt32BE(totalLength, 4);
fs.writeFileSync(outputPath, Buffer.concat([header, ...chunks], totalLength));
console.log(`Wrote ${outputPath}`);
