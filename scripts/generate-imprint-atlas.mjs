import { spawn } from "node:child_process";
import { mkdir, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repository = path.resolve(scriptDirectory, "..");
const sketchIndex = path.resolve(
  process.env.S3G_IMPRINT_SKETCH || path.join(repository, "../s3g-mc/Scripts/s3g-mc/utilities/imprint-sketch-designer/index.html")
);
const outputDirectory = path.join(repository, "plugins/clap_ambi_imprint/atlas");
const projectsDirectory = path.join(outputDirectory, "projects");
const chrome = process.env.CHROME_BIN || "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
const port = Number(process.env.S3G_IMPRINT_ATLAS_PORT || 9337);

const presets = [
  {
    slug: "architecture_concrete_gallery",
    title: "Concrete Gallery",
    category: "Architecture",
    description: "A broad concrete gallery with offset stone branches and restrained irregularity.",
    family: "room", seed: 110103, bias: 0.24, branchFamily: "room", material: "concrete",
    overrides: { spaceShape: "side_chamber", roomShape: "trapezoid", chamberShape: "skew", chamberMaterial: "stone", chamberMaterialMode: "alternating", chamberCount: 2, nestedChambers: 1, outsideOpening: false }
  },
  {
    slug: "architecture_wood_studio",
    title: "Wood Studio",
    category: "Architecture",
    description: "A compact wood room with a damped architectural annex and controlled tail.",
    family: "room", seed: 110207, bias: 0.12, branchFamily: "room", material: "wood",
    overrides: { spaceShape: "side_chamber", roomShape: "rect", chamberShape: "trapezoid", chamberMaterial: "damped", chamberMaterialMode: "uniform", chamberCount: 1, nestedChambers: 0, openness: 0.02, outsideOpening: false, duration: 1.8 }
  },
  {
    slug: "cave_limestone_pocket",
    title: "Limestone Pocket",
    category: "Cave",
    description: "A rough limestone pocket with smaller cave branches and dense directional scatter.",
    family: "cave", seed: 220103, bias: 0.48, branchFamily: "cave", material: "stone",
    overrides: { spaceShape: "side_chamber", chamberMaterial: "porous_rock", chamberMaterialMode: "nested", chamberCount: 2, nestedChambers: 1, openness: 0.03, outsideOpening: false }
  },
  {
    slug: "cave_ice_grotto",
    title: "Ice Grotto",
    category: "Cave",
    description: "A reflective ice grotto opening into a water-worn cavern pocket.",
    family: "cave", seed: 220211, bias: 0.61, branchFamily: "cavern", material: "ice",
    overrides: { spaceShape: "side_chamber", chamberMaterial: "water", chamberMaterialMode: "alternating", chamberCount: 2, nestedChambers: 1, verticalVariation: 0.72, outsideOpening: true, outsideLeak: 0.13 }
  },
  {
    slug: "cavern_deep_stone_vault",
    title: "Deep Stone Vault",
    category: "Cavern",
    description: "A tall stone vault with long coupled cavern returns and a sparse exterior leak.",
    family: "cavern", seed: 330107, bias: 0.44, branchFamily: "cavern", material: "stone",
    overrides: { spaceShape: "side_chamber", chamberMaterial: "porous_rock", chamberMaterialMode: "nested", chamberCount: 2, nestedChambers: 2, chamberCoupling: 0.72, duration: 7.5, outsideOpening: true, outsideLeak: 0.08 }
  },
  {
    slug: "cavern_water_chamber",
    title: "Water Chamber",
    category: "Cavern",
    description: "A wide cavern whose wet chamber network brightens and disperses the early field.",
    family: "cavern", seed: 330223, bias: 0.57, branchFamily: "mixed", material: "water",
    overrides: { spaceShape: "side_chamber", chamberMaterial: "stone", chamberMaterialMode: "palette", chamberCount: 3, nestedChambers: 1, chamberSide: "all", openness: 0.11, outsideOpening: true }
  },
  {
    slug: "tunnel_brick_bend",
    title: "Brick Bend",
    category: "Tunnel",
    description: "A bent brick passage with narrow axial returns and one architectural side room.",
    family: "tunnel", seed: 440117, bias: 0.36, branchFamily: "room", material: "brick",
    overrides: { spaceShape: "side_chamber", chamberMaterial: "brick", chamberMaterialMode: "uniform", chamberCount: 1, nestedChambers: 0, chamberSide: "right", chamberCoupling: 0.54, outsideOpening: true, outsideLeak: 0.31 }
  },
  {
    slug: "tunnel_metal_conduit",
    title: "Metal Conduit",
    category: "Tunnel",
    description: "A tight metallic conduit with nested tunnel continuations and focused axial energy.",
    family: "tunnel", seed: 440229, bias: 0.68, branchFamily: "tunnel", material: "metal",
    overrides: { spaceShape: "side_chamber", chamberMaterial: "metal", chamberMaterialMode: "uniform", chamberCount: 2, nestedChambers: 2, chamberCoupling: 0.82, openness: 0.08, outsideOpening: true, outsideLeak: 0.22 }
  },
  {
    slug: "canyon_dry_slot",
    title: "Dry Slot",
    category: "Canyon",
    description: "A narrow dry slot with tall stone walls, open sky, and sparse delayed returns.",
    family: "canyon", seed: 550101, bias: 0.34, branchFamily: "canyon", material: "earth",
    overrides: { spaceShape: "side_chamber", chamberMaterial: "stone", chamberMaterialMode: "alternating", chamberCount: 1, nestedChambers: 1, openness: 0.79, outsideOpening: true, outsideLeak: 0.76, duration: 3.6 }
  },
  {
    slug: "canyon_porous_gorge",
    title: "Porous Gorge",
    category: "Canyon",
    description: "A broad porous gorge with cave recesses and strongly thinned late energy.",
    family: "canyon", seed: 550239, bias: 0.63, branchFamily: "cave", material: "porous_rock",
    overrides: { spaceShape: "side_chamber", chamberMaterial: "earth", chamberMaterialMode: "palette", chamberCount: 2, nestedChambers: 1, chamberSide: "all", openness: 0.72, outsideOpening: true, outsideLeak: 0.68 }
  },
  {
    slug: "clearing_forest_ring",
    title: "Forest Ring",
    category: "Clearing",
    description: "An open vegetation ring with low boundary pockets and diffuse distant scatter.",
    family: "clearing", seed: 660109, bias: 0.31, branchFamily: "clearing", material: "vegetation",
    overrides: { spaceShape: "side_chamber", chamberMaterial: "earth", chamberMaterialMode: "alternating", chamberCount: 2, nestedChambers: 0, openness: 0.91, outsideOpening: true, outsideLeak: 0.84, duration: 1.6 }
  },
  {
    slug: "clearing_water_meadow",
    title: "Water Meadow",
    category: "Clearing",
    description: "A wet open field with water and vegetation boundaries plus a few cave-like hollows.",
    family: "clearing", seed: 660241, bias: 0.52, branchFamily: "mixed", material: "water",
    overrides: { spaceShape: "side_chamber", chamberMaterial: "vegetation", chamberMaterialMode: "palette", chamberCount: 2, nestedChambers: 1, openness: 0.86, outsideOpening: true, outsideLeak: 0.72 }
  },
  {
    slug: "abstract_folded_chamber",
    title: "Folded Chamber",
    category: "Abstract",
    description: "An irregular folded field whose connected regions alternate between ordinary and impossible spaces.",
    family: "abstract", seed: 770113, bias: 0.78, branchFamily: "mixed", material: "glass",
    overrides: { spaceShape: "side_chamber", roomShape: "impossible", chamberShape: "impossible", chamberMaterial: "metal", chamberMaterialMode: "palette", chamberCount: 3, nestedChambers: 1, chamberSide: "all", chamberCoupling: 0.77 }
  },
  {
    slug: "abstract_impossible_network",
    title: "Impossible Network",
    category: "Abstract",
    description: "A high-contrast network of mixed spatial families, materials, scales, and coupling paths.",
    family: "abstract", seed: 770257, bias: 0.96, branchFamily: "mixed", material: "fabric",
    overrides: { spaceShape: "side_chamber", roomShape: "impossible", chamberShape: "impossible", chamberMaterial: "ice", chamberMaterialMode: "palette", chamberCount: 4, nestedChambers: 2, chamberSide: "all", chamberCoupling: 0.92, groupVariation: 0.84, surfaceContrast: 0.91, distanceVariation: 0.68 }
  }
];

function validateImprint(imprint, preset) {
  const fail = (message) => { throw new Error(`${preset.slug}: ${message}`); };
  if (imprint?.format !== "s3g-ambi-imprint" || imprint.version !== 1) fail("invalid format or version");
  if (imprint.space?.family !== preset.family) fail(`expected ${preset.family} family`);
  if (imprint.space?.branch_family_mode !== preset.branchFamily) fail(`expected ${preset.branchFamily} branch mode`);
  if (!Array.isArray(imprint.room?.polygon_xy_m) || imprint.room.polygon_xy_m.length < 3) fail("missing primary polygon");
  if (!Array.isArray(imprint.profiles) || imprint.profiles.length < 1 || imprint.profiles.length > 8) fail("expected 1-8 profiles");
  for (const profile of imprint.profiles) {
    if (!Array.isArray(profile.early_reflections)) fail("profile is missing early reflections");
    if (!Array.isArray(profile.late?.absorption_by_band) || profile.late.absorption_by_band.length !== 8) fail("invalid absorption bands");
    if (!Array.isArray(profile.late?.rt60_s_by_band) || profile.late.rt60_s_by_band.length !== 8) fail("invalid RT60 bands");
    for (const event of profile.early_reflections) {
      if (![event.delay_ms, event.gain, event.azimuth_deg, event.elevation_deg].every(Number.isFinite)) fail("non-finite early reflection");
    }
  }
  const branches = imprint.room?.chamber?.chambers || [];
  if (preset.overrides?.spaceShape === "side_chamber" && branches.length < 1) fail("connected preset has no branches");
  for (const branch of branches) {
    if (!Array.isArray(branch.polygon) || branch.polygon.length < 3) fail("branch polygon is invalid");
    if (!Number.isFinite(branch.height_m) || branch.height_m <= 0) fail("branch height is invalid");
  }
}

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
  `--user-data-dir=/tmp/s3g-imprint-atlas-chrome-${process.pid}`,
  pathToFileURL(sketchIndex).href
], { stdio: "ignore" });

try {
  const targets = await waitForEndpoint(endpoint);
  const target = targets.find((item) => item.type === "page" && item.url.includes("imprint-sketch-designer"));
  if (!target) throw new Error(`Imprint Sketch target not found for ${sketchIndex}`);
  const client = await connect(target);
  await client.send("Runtime.enable");

  const manifest = {
    format: "s3g-imprint-atlas",
    version: 1,
    generator: "s3g-mc Imprint Sketch",
    presets: []
  };

  for (const preset of presets) {
    const expression = `(() => {
      const preset = ${JSON.stringify(preset)};
      resetDefaults();
      controls.spaceFamily.value = preset.family;
      controls.topologyBias.value = preset.bias;
      controls.branchFamily.value = preset.branchFamily;
      randomize(preset.seed);
      if (preset.material && materials[preset.material]) {
        controls.materialPreset.value = preset.material;
        controls.absorption.value = materials[preset.material].absorption;
        controls.scattering.value = materials[preset.material].scattering;
        controls.tailSoften.value = materials[preset.material].tailSoften;
      }
      Object.entries(preset.overrides || {}).forEach(([key, value]) => {
        const control = controls[key];
        if (!control) throw new Error("Unknown Imprint Sketch control: " + key);
        if (control.type === "checkbox") control.checked = Boolean(value);
        else control.value = value;
      });
      const current = settings();
      const imprint = imprintObject(current);
      const project = exportObject(current);
      imprint.atlas = {
        name: preset.title,
        category: preset.category,
        description: preset.description,
        seed: preset.seed,
        source: "s3g-mc Imprint Sketch"
      };
      project.atlas = imprint.atlas;
      return { imprint, project };
    })()`;
    const evaluation = await client.send("Runtime.evaluate", { expression, returnByValue: true, awaitPromise: true });
    if (evaluation.exceptionDetails) {
      throw new Error(evaluation.exceptionDetails.exception?.description || evaluation.exceptionDetails.text);
    }
    const { imprint, project } = evaluation.result.value;
    validateImprint(imprint, preset);
    if (project?.format !== "s3g-imprint-sketch" || project.version !== 1) throw new Error(`${preset.slug}: invalid editable project`);
    const filename = `${preset.slug}.s3gimprint`;
    const projectFilename = `${preset.slug}.json`;
    await writeFile(path.join(outputDirectory, filename), `${JSON.stringify(imprint, null, 2)}\n`);
    await writeFile(path.join(projectsDirectory, projectFilename), `${JSON.stringify(project, null, 2)}\n`);
    manifest.presets.push({
      name: preset.title,
      category: preset.category,
      description: preset.description,
      file: filename,
      project_file: `projects/${projectFilename}`,
      family: imprint.space?.family,
      branch_family_mode: imprint.space?.branch_family_mode,
      seed: preset.seed,
      duration_s: imprint.duration_s,
      profiles: imprint.profiles?.length || 0
    });
  }

  await writeFile(path.join(outputDirectory, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);
  client.socket.close();
  process.stdout.write(`Generated ${manifest.presets.length} Imprint Atlas spaces in ${outputDirectory}\n`);
} finally {
  chromeProcess.kill("SIGTERM");
}
