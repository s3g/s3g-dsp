import { readdir, readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { validateRayAtlasField } from "./ray-atlas-validation.mjs";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repository = path.resolve(scriptDirectory, "..");
const atlasDirectory = path.join(repository, "plugins/clap_ambi_ray_encoder/atlas");
const manifest = JSON.parse(await readFile(path.join(atlasDirectory, "manifest.json"), "utf8"));

if (manifest?.format !== "s3g-ray-atlas" || manifest.version !== 1) throw new Error("Invalid Ray Atlas manifest");
if (!Array.isArray(manifest.presets) || manifest.presets.length !== 19) throw new Error("Ray Atlas must contain 19 presets");

const listedFiles = new Set();
let totalCells = 0;
let totalReflections = 0;
let totalBounces = 0;
for (const preset of manifest.presets) {
  if (!preset.file || listedFiles.has(preset.file)) throw new Error(`Duplicate or missing atlas file: ${preset.file || "-"}`);
  listedFiles.add(preset.file);
  const raw = await readFile(path.join(atlasDirectory, preset.file), "utf8");
  const field = JSON.parse(raw);
  const slug = path.basename(preset.file, ".s3gray");
  const counts = validateRayAtlasField(field, { slug, name: preset.name, category: preset.category });
  if (preset.cells !== field.cells.length
      || preset.reflection_slots !== field.grid.reflection_slots
      || preset.reflections !== counts.reflectionCount
      || preset.bounce_reflections !== counts.bounceCount
      || preset.bytes !== Buffer.byteLength(raw)) throw new Error(`${slug}: manifest statistics are stale`);
  totalCells += field.cells.length;
  totalReflections += counts.reflectionCount;
  totalBounces += counts.bounceCount;
}

const packagedFiles = (await readdir(atlasDirectory)).filter((name) => name.endsWith(".s3gray"));
if (packagedFiles.length !== listedFiles.size || packagedFiles.some((name) => !listedFiles.has(name))) {
  throw new Error("Ray Atlas manifest and packaged fields do not match");
}

process.stdout.write(`Ray Atlas check passed: ${listedFiles.size} spaces, ${totalCells} cells, ${totalReflections} reflections, ${totalBounces} bounce paths\n`);
