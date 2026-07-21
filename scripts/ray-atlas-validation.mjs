function finitePoint(point) {
  return point && [point.x, point.y, point.z].every(Number.isFinite);
}

export const RAY_WORLD_TO_AED_CONVENTION = "azimuth_deg=atan2(-x_right,y_front)";

export function validateRayAtlasField(field, preset) {
  const fail = (message) => { throw new Error(`${preset.slug}: ${message}`); };
  if (field?.format !== "s3g-ambi-ray-field" || field.version !== 1) fail("invalid format or version");
  if (field.coordinate_system?.world_to_aed !== RAY_WORLD_TO_AED_CONVENTION
      || field.coordinate_system?.azimuth_positive !== "counterclockwise") fail("invalid world-to-AED convention");
  if (field.atlas?.name !== preset.name || field.atlas?.category !== preset.category) fail("atlas metadata mismatch");
  if (!Array.isArray(field.room?.polygon_xy_m) || field.room.polygon_xy_m.length < 3) fail("missing room polygon");
  if (!finitePoint(field.listener_position_m) || !finitePoint(field.default_source_position_m)) fail("invalid endpoint position");
  const minimum = field.room?.navigation_bounds_m?.minimum;
  const maximum = field.room?.navigation_bounds_m?.maximum;
  if (!finitePoint(minimum) || !finitePoint(maximum)
      || maximum.x <= minimum.x || maximum.y <= minimum.y || maximum.z <= minimum.z) fail("invalid navigation bounds");
  if (!Array.isArray(field.cells) || field.cells.length < 1 || field.cells.length > 256) fail("expected 1-256 source cells");
  const slotCount = field.grid?.reflection_slots;
  if (!Number.isInteger(slotCount) || slotCount < 4 || slotCount > 32) fail("invalid reflection slot count");

  let reflectionCount = 0;
  let bounceCount = 0;
  for (const cell of field.cells) {
    if (!finitePoint(cell.position_m)) fail("invalid cell position");
    if (!Array.isArray(cell.early_reflections) || cell.early_reflections.length > slotCount) fail("invalid reflection array");
    const occupiedSlots = new Set();
    for (const reflection of cell.early_reflections) {
      if (!Number.isInteger(reflection.slot) || reflection.slot < 0 || reflection.slot >= slotCount) fail("reflection slot out of range");
      if (occupiedSlots.has(reflection.slot)) fail("duplicate reflection slot");
      occupiedSlots.add(reflection.slot);
      if (![reflection.delay_ms, reflection.gain, reflection.azimuth_deg,
        reflection.elevation_deg, reflection.damping].every(Number.isFinite)) fail("non-finite reflection value");
      if (reflection.delay_ms < 0 || reflection.delay_ms > 6000) fail("reflection exceeds runtime delay range");
      if (reflection.bounce_position_m !== undefined) {
        if (!finitePoint(reflection.bounce_position_m)) fail("invalid bounce position");
        bounceCount += 1;
      }
      reflectionCount += 1;
    }
    if (![cell.late?.start_ms, cell.late?.decay_s, cell.late?.level,
      cell.late?.diffusion, cell.late?.damping].every(Number.isFinite)) fail("invalid late profile");
  }
  if (reflectionCount < field.cells.length) fail("field has too few early reflections");
  if (bounceCount < field.cells.length) fail("field has too few listener-resolvable bounce paths");
  return { reflectionCount, bounceCount };
}
