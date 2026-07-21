import { spawn } from "node:child_process";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

import { RAY_WORLD_TO_AED_CONVENTION, validateRayAtlasField } from "./ray-atlas-validation.mjs";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repository = path.resolve(scriptDirectory, "..");
const sketchIndex = path.resolve(
  process.env.S3G_RAY_SKETCH || path.join(repository, "../s3g-mc/Scripts/s3g-mc/utilities/ray-sketch-designer/index.html")
);
const imprintAtlasDirectory = path.join(repository, "plugins/clap_ambi_imprint/atlas");
const outputDirectory = path.join(repository, "plugins/clap_ambi_ray_encoder/atlas");
const projectsDirectory = path.join(outputDirectory, "projects");
const chrome = process.env.CHROME_BIN || "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
const port = Number(process.env.S3G_RAY_ATLAS_PORT || 9338);

async function waitForEndpoint(endpoint, attempts = 100) {
  for (let attempt = 0; attempt < attempts; attempt += 1) {
    try {
      const response = await fetch(`${endpoint}/json`);
      if (response.ok) return response.json();
    } catch {
      // Chrome is still starting.
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error("Timed out waiting for headless Chrome");
}

async function connect(target) {
  const socket = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => {
    socket.addEventListener("open", resolve, { once: true });
    socket.addEventListener("error", reject, { once: true });
  });
  let nextId = 1;
  const pending = new Map();
  socket.addEventListener("message", (message) => {
    const payload = JSON.parse(message.data);
    if (!payload.id || !pending.has(payload.id)) return;
    const handler = pending.get(payload.id);
    pending.delete(payload.id);
    if (payload.error) handler.reject(new Error(payload.error.message));
    else handler.resolve(payload.result);
  });
  return {
    socket,
    send(method, params = {}) {
      const id = nextId++;
      socket.send(JSON.stringify({ id, method, params }));
      return new Promise((resolve, reject) => pending.set(id, { resolve, reject }));
    }
  };
}

const sourceManifest = JSON.parse(await readFile(path.join(imprintAtlasDirectory, "manifest.json"), "utf8"));
const presets = sourceManifest.presets.map((preset) => ({
  slug: path.basename(preset.project_file, ".json"),
  name: preset.name,
  category: preset.category,
  description: preset.description,
  seed: preset.seed,
  projectPath: path.join(imprintAtlasDirectory, preset.project_file)
}));

await mkdir(outputDirectory, { recursive: true });
await mkdir(projectsDirectory, { recursive: true });
const endpoint = `http://127.0.0.1:${port}`;
const chromeProcess = spawn(chrome, [
  "--headless=new",
  "--disable-background-networking",
  "--disable-gpu",
  "--no-first-run",
  "--no-default-browser-check",
  `--remote-debugging-port=${port}`,
  `--user-data-dir=/tmp/s3g-ray-atlas-chrome-${process.pid}`,
  pathToFileURL(sketchIndex).href
], { stdio: "ignore" });

try {
  const targets = await waitForEndpoint(endpoint);
  const target = targets.find((item) => item.type === "page" && item.url.includes("ray-sketch-designer"));
  if (!target) throw new Error(`Ray Sketch target not found for ${sketchIndex}`);
  const client = await connect(target);
  await client.send("Runtime.enable");
  for (let attempt = 0; attempt < 100; attempt += 1) {
    const ready = await client.send("Runtime.evaluate", {
      expression: "typeof resetDefaults === 'function' && typeof applyExportObject === 'function' && typeof rayFieldObject === 'function'",
      returnByValue: true
    });
    if (ready.result?.value === true) break;
    if (attempt === 99) throw new Error("Ray Sketch did not finish loading");
    await new Promise((resolve) => setTimeout(resolve, 50));
  }

  const manifest = {
    format: "s3g-ray-atlas",
    version: 1,
    generator: "s3g-mc Ray Sketch",
    presets: []
  };

  for (const preset of presets) {
    const sourceProject = JSON.parse(await readFile(preset.projectPath, "utf8"));
    const expression = `(() => {
      const sourceProject = ${JSON.stringify(sourceProject)};
      const atlas = ${JSON.stringify({
        name: preset.name,
        category: preset.category,
        description: preset.description,
        seed: preset.seed,
        source: "s3g-mc Ray Sketch"
      })};
      resetDefaults();
      applyExportObject(sourceProject);
      controls.rayGridX.value = 5;
      controls.rayGridY.value = 5;
      controls.rayGridZ.value = 3;
      controls.raySlots.value = 24;
      const current = settings();
      const field = rayFieldObject(current);
      const project = exportObject(current);
      field.interpretation = "musical moving-source and moving-listener ray field; geometry and material values are creative estimates";
      field.coordinate_system.azimuth_positive = "counterclockwise";
      field.coordinate_system.world_to_aed = ${JSON.stringify(RAY_WORLD_TO_AED_CONVENTION)};
      field.cells.forEach((cell) => cell.early_reflections.forEach((reflection) => {
        reflection.azimuth_deg = round(wrapDegrees(-reflection.azimuth_deg), 3);
      }));
      field.atlas = atlas;
      project.atlas = atlas;
      return { field, project };
    })()`;
    const evaluation = await client.send("Runtime.evaluate", { expression, returnByValue: true, awaitPromise: true });
    if (evaluation.exceptionDetails) {
      throw new Error(evaluation.exceptionDetails.exception?.description || evaluation.exceptionDetails.text);
    }
    const { field, project } = evaluation.result.value;
    const counts = validateRayAtlasField(field, preset);
    if (project?.format !== "s3g-ray-sketch" || project.version !== 1) throw new Error(`${preset.slug}: invalid editable project`);
    const filename = `${preset.slug}.s3gray`;
    const projectFilename = `${preset.slug}.json`;
    const fieldJson = `${JSON.stringify(field, null, 2)}\n`;
    await writeFile(path.join(outputDirectory, filename), fieldJson);
    await writeFile(path.join(projectsDirectory, projectFilename), `${JSON.stringify(project, null, 2)}\n`);
    manifest.presets.push({
      name: preset.name,
      category: preset.category,
      description: preset.description,
      file: filename,
      project_file: `projects/${projectFilename}`,
      family: field.room?.family,
      seed: preset.seed,
      duration_s: field.duration_s,
      cells: field.cells.length,
      reflection_slots: field.grid.reflection_slots,
      reflections: counts.reflectionCount,
      bounce_reflections: counts.bounceCount,
      bytes: Buffer.byteLength(fieldJson)
    });
  }

  await writeFile(path.join(outputDirectory, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);
  client.socket.close();
  process.stdout.write(`Generated ${manifest.presets.length} Ray Atlas spaces in ${outputDirectory}\n`);
} finally {
  chromeProcess.kill("SIGTERM");
}
