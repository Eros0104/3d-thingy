// fps-engine level editor (v3 — sectors + planar walls + openings).
// Single-file vanilla JS; no build step. Writes v3 JSON matching src/json_level.cpp.
//
// Opening model: an opening (door/window) is a world-space object with a center
// position and a direction.  Wall indices are never stored in the editor — they are
// computed automatically at export time.  This means one opening carves through
// every wall that it overlaps, including both faces of a thick-wall pair.

const WALL_COLOR       = '#78a4d6';
const WALL_COLOR_SEL   = '#8bc4ff';
const DOOR_COLOR       = '#f0a040';
const DOOR_COLOR_SEL   = '#ffaa44';
const WINDOW_COLOR     = '#44d4f0';
const WINDOW_COLOR_SEL = '#88eeff';

const level = {
  version: 3,
  name: 'untitled',
  wall_height: 3.2,
  ambient: [0.07, 0.08, 0.11],
  spawn: { pos: [0, 0, 0], yaw_deg: 0 },
  sectors: [],
  walls: [],
  openings: [], // world-space, converted to brushes on export
  stairs: [],
  lights: [],
};

const editor = {
  tool: 'select',
  wall_thickness: 0.2,
  wall_pairs: [], // [[primary_idx, secondary_idx], ...] — editor-only, not exported
  default_floor_y: 0.0,
  default_ceiling_y: 3.2,
  snap: true,
  grid_step: 0.5,
  view: { ox: 600, oy: 400, scale: 20 },
  draft: null,
  selection: null,
  mouse_world: { x: 0, z: 0 },
  mouse_screen: { x: 0, y: 0 },
  space_held: false,
  panning: false,
  pan_start: null,
  dragging: false,
};

const canvas = document.getElementById('grid');
const ctx = canvas.getContext('2d');

function resize_canvas() {
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  canvas.width = Math.floor(rect.width * dpr);
  canvas.height = Math.floor(rect.height * dpr);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  render();
}
window.addEventListener('resize', resize_canvas);

// -------- World <-> screen -----------------------------------------------------------

function world_to_screen(wx, wz) {
  return { x: editor.view.ox + wx * editor.view.scale, y: editor.view.oy + wz * editor.view.scale };
}

function screen_to_world(sx, sy) {
  return { x: (sx - editor.view.ox) / editor.view.scale, z: (sy - editor.view.oy) / editor.view.scale };
}

function snap_world(w) {
  if (!editor.snap) return w;
  const s = editor.grid_step;
  return { x: Math.round(w.x / s) * s, z: Math.round(w.z / s) * s };
}

// -------- Rendering ------------------------------------------------------------------

function render() {
  const w = canvas.clientWidth;
  const h = canvas.clientHeight;
  ctx.clearRect(0, 0, w, h);
  draw_grid(w, h);
  draw_sectors();
  draw_wall_pairs();
  draw_walls();
  draw_openings();
  draw_stairs();
  draw_lights();
  draw_spawn();
  draw_draft();
  draw_selection_handles();
}

function draw_grid(w, h) {
  const step_world = editor.grid_step;
  const step_px = step_world * editor.view.scale;
  if (step_px < 4) return;
  const world_left   = -editor.view.ox / editor.view.scale;
  const world_top    = -editor.view.oy / editor.view.scale;
  const world_right  = (w - editor.view.ox) / editor.view.scale;
  const world_bottom = (h - editor.view.oy) / editor.view.scale;
  ctx.strokeStyle = '#1e2230';
  ctx.lineWidth = 1;
  ctx.beginPath();
  const x0 = Math.ceil(world_left / step_world) * step_world;
  for (let x = x0; x <= world_right; x += step_world) {
    const s = world_to_screen(x, 0).x;
    ctx.moveTo(s, 0); ctx.lineTo(s, h);
  }
  const z0 = Math.ceil(world_top / step_world) * step_world;
  for (let z = z0; z <= world_bottom; z += step_world) {
    const s = world_to_screen(0, z).y;
    ctx.moveTo(0, s); ctx.lineTo(w, s);
  }
  ctx.stroke();
  ctx.strokeStyle = '#3a3f4f';
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  const oy = world_to_screen(0, 0).y;
  const ox = world_to_screen(0, 0).x;
  ctx.moveTo(0, oy); ctx.lineTo(w, oy);
  ctx.moveTo(ox, 0); ctx.lineTo(ox, h);
  ctx.stroke();
}

function draw_sectors() {
  for (let i = 0; i < level.sectors.length; ++i) {
    const s = level.sectors[i];
    if (s.polygon.length < 3) continue;
    ctx.beginPath();
    const p0 = world_to_screen(s.polygon[0][0], s.polygon[0][1]);
    ctx.moveTo(p0.x, p0.y);
    for (let k = 1; k < s.polygon.length; ++k) {
      const p = world_to_screen(s.polygon[k][0], s.polygon[k][1]);
      ctx.lineTo(p.x, p.y);
    }
    ctx.closePath();
    ctx.fillStyle = `rgba(58, 63, 79, ${Math.min(0.55, 0.25 + s.floor_y * 0.05)})`;
    ctx.fill();
    ctx.strokeStyle = '#555b73';
    ctx.lineWidth = 1;
    ctx.stroke();
    const cx = s.polygon.reduce((a, p) => a + p[0], 0) / s.polygon.length;
    const cz = s.polygon.reduce((a, p) => a + p[1], 0) / s.polygon.length;
    const sp = world_to_screen(cx, cz);
    ctx.fillStyle = '#8b92a3';
    ctx.font = '10px ui-monospace, monospace';
    ctx.textAlign = 'center';
    ctx.fillText((s.id || `sector ${i}`) + ` · y=${s.floor_y}`, sp.x, sp.y);
  }
}

// -------- Wall pair helpers ----------------------------------------------------------

function find_pair_partner(wall_idx) {
  for (const [a, b] of editor.wall_pairs) {
    if (a === wall_idx) return b;
    if (b === wall_idx) return a;
  }
  return -1;
}

function pair_screen_quad(w1, w2) {
  const dx1 = w1.b[0] - w1.a[0], dz1 = w1.b[2] - w1.a[2];
  const dx2 = w2.b[0] - w2.a[0], dz2 = w2.b[2] - w2.a[2];
  const same_dir = dx1 * dx2 + dz1 * dz2 >= 0;
  const p0 = world_to_screen(w1.a[0], w1.a[2]);
  const p1 = world_to_screen(w1.b[0], w1.b[2]);
  const p2 = same_dir ? world_to_screen(w2.b[0], w2.b[2]) : world_to_screen(w2.a[0], w2.a[2]);
  const p3 = same_dir ? world_to_screen(w2.a[0], w2.a[2]) : world_to_screen(w2.b[0], w2.b[2]);
  return [p0, p1, p2, p3];
}

function pair_partner_sub(w1, w2, sel_sub) {
  const dx1 = w1.b[0] - w1.a[0], dz1 = w1.b[2] - w1.a[2];
  const dx2 = w2.b[0] - w2.a[0], dz2 = w2.b[2] - w2.a[2];
  const same_dir = dx1 * dx2 + dz1 * dz2 >= 0;
  return same_dir ? sel_sub : (sel_sub === 'a' ? 'b' : 'a');
}

function draw_wall_pairs() {
  for (const [i, j] of editor.wall_pairs) {
    const w1 = level.walls[i], w2 = level.walls[j];
    if (!w1 || !w2) continue;
    const pts = pair_screen_quad(w1, w2);
    const sel_idx = (editor.selection?.kind === 'wall' || editor.selection?.kind === 'wall_vertex')
      ? editor.selection.idx : -1;
    const is_active = sel_idx === i || sel_idx === j;
    ctx.beginPath();
    ctx.moveTo(pts[0].x, pts[0].y);
    for (let k = 1; k < 4; k++) ctx.lineTo(pts[k].x, pts[k].y);
    ctx.closePath();
    ctx.fillStyle = is_active ? 'rgba(139,196,255,0.18)' : 'rgba(120,164,214,0.08)';
    ctx.fill();
  }
}

function delete_walls_at_indices(indices) {
  const sorted = [...indices].sort((a, b) => b - a);
  for (const idx of sorted) {
    level.walls.splice(idx, 1);
    editor.wall_pairs = editor.wall_pairs
      .filter(([a, b]) => a !== idx && b !== idx)
      .map(([a, b]) => [a > idx ? a - 1 : a, b > idx ? b - 1 : b]);
  }
}

// Walls: a = [x, y, z], b = [x, y, z]. Top-down view uses x (index 0) and z (index 2).
function draw_walls() {
  const sel_idx = (editor.selection?.kind === 'wall' || editor.selection?.kind === 'wall_vertex')
    ? editor.selection.idx : -1;
  for (let i = 0; i < level.walls.length; ++i) {
    const wall = level.walls[i];
    const a = world_to_screen(wall.a[0], wall.a[2]);
    const b = world_to_screen(wall.b[0], wall.b[2]);
    const partner = find_pair_partner(i);
    const highlighted = i === sel_idx || (partner >= 0 && partner === sel_idx);
    ctx.strokeStyle = highlighted ? WALL_COLOR_SEL : WALL_COLOR;
    ctx.lineWidth = 3;
    ctx.lineCap = 'square';
    ctx.beginPath();
    ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y);
    ctx.stroke();
  }
  ctx.lineCap = 'butt';
}

// Draw openings as gaps on whatever walls they overlap.
// Opening: { kind, cx, cz, dx, dz, width, height, y_start }
function draw_openings() {
  for (let i = 0; i < level.openings.length; ++i) {
    const op = level.openings[i];
    const hx = op.dx * op.width / 2, hz = op.dz * op.width / 2;
    const p0 = world_to_screen(op.cx - hx, op.cz - hz);
    const p1 = world_to_screen(op.cx + hx, op.cz + hz);
    ctx.strokeStyle = '#0c0e13';
    ctx.lineWidth = 5;
    ctx.lineCap = 'butt';
    ctx.beginPath(); ctx.moveTo(p0.x, p0.y); ctx.lineTo(p1.x, p1.y); ctx.stroke();
    const is_sel = editor.selection?.kind === 'opening' && editor.selection.idx === i;
    if (op.kind === 'window') {
      ctx.strokeStyle = is_sel ? WINDOW_COLOR_SEL : WINDOW_COLOR;
    } else {
      ctx.strokeStyle = is_sel ? DOOR_COLOR_SEL : DOOR_COLOR;
    }
    ctx.lineWidth = 2;
    ctx.setLineDash([3, 3]);
    ctx.beginPath(); ctx.moveTo(p0.x, p0.y); ctx.lineTo(p1.x, p1.y); ctx.stroke();
    ctx.setLineDash([]);
  }
}

function draw_stairs() {
  for (const s of level.stairs) {
    const dx = s.center_b[0] - s.center_a[0];
    const dz = s.center_b[1] - s.center_a[1];
    const len = Math.sqrt(dx * dx + dz * dz);
    if (len < 1e-4) continue;
    const ux = dx / len, uz = dz / len;
    const nx = -uz, nz = ux;
    const hw = s.width / 2;
    const corners = [
      [s.center_a[0] + nx * hw, s.center_a[1] + nz * hw],
      [s.center_b[0] + nx * hw, s.center_b[1] + nz * hw],
      [s.center_b[0] - nx * hw, s.center_b[1] - nz * hw],
      [s.center_a[0] - nx * hw, s.center_a[1] - nz * hw],
    ];
    ctx.beginPath();
    const pp0 = world_to_screen(corners[0][0], corners[0][1]);
    ctx.moveTo(pp0.x, pp0.y);
    for (let k = 1; k < 4; ++k) {
      const p = world_to_screen(corners[k][0], corners[k][1]);
      ctx.lineTo(p.x, p.y);
    }
    ctx.closePath();
    ctx.fillStyle = 'rgba(232, 184, 78, 0.2)';
    ctx.fill();
    ctx.strokeStyle = '#e8b84e';
    ctx.lineWidth = 1.5;
    ctx.stroke();
    ctx.strokeStyle = '#e8b84e';
    ctx.lineWidth = 0.5;
    ctx.beginPath();
    for (let i = 1; i < s.steps; ++i) {
      const t = i / s.steps;
      const mx = s.center_a[0] + dx * t, mz = s.center_a[1] + dz * t;
      const a = world_to_screen(mx + nx * hw, mz + nz * hw);
      const b = world_to_screen(mx - nx * hw, mz - nz * hw);
      ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y);
    }
    ctx.stroke();
    const startS = world_to_screen(s.center_a[0], s.center_a[1]);
    const endS   = world_to_screen(s.center_b[0], s.center_b[1]);
    ctx.strokeStyle = '#e8b84e';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(startS.x, startS.y); ctx.lineTo(endS.x, endS.y);
    ctx.stroke();
    draw_arrowhead(endS.x, endS.y, Math.atan2(endS.y - startS.y, endS.x - startS.x), '#e8b84e');
    const midx = (s.center_a[0] + s.center_b[0]) / 2;
    const midz = (s.center_a[1] + s.center_b[1]) / 2;
    const m = world_to_screen(midx, midz);
    ctx.fillStyle = '#e8b84e';
    ctx.font = '10px ui-monospace, monospace';
    ctx.textAlign = 'center';
    ctx.fillText(`↑ ${s.from_y}→${s.to_y}`, m.x, m.y - 4);
  }
}

function draw_arrowhead(x, y, ang, color) {
  const size = 8;
  ctx.fillStyle = color;
  ctx.beginPath();
  ctx.moveTo(x, y);
  ctx.lineTo(x - size * Math.cos(ang - 0.35), y - size * Math.sin(ang - 0.35));
  ctx.lineTo(x - size * Math.cos(ang + 0.35), y - size * Math.sin(ang + 0.35));
  ctx.closePath();
  ctx.fill();
}

function draw_lights() {
  for (const L of level.lights) {
    const p = world_to_screen(L.pos[0], L.pos[2]);
    ctx.fillStyle = '#f3e97a';
    ctx.beginPath(); ctx.arc(p.x, p.y, 5, 0, Math.PI * 2); ctx.fill();
    ctx.strokeStyle = '#0c0e13'; ctx.lineWidth = 1; ctx.stroke();
    ctx.fillStyle = '#f3e97a';
    ctx.font = '9px ui-monospace, monospace';
    ctx.textAlign = 'left';
    ctx.fillText(`y=${L.pos[1]}`, p.x + 7, p.y + 3);
  }
}

function draw_spawn() {
  const p = world_to_screen(level.spawn.pos[0], level.spawn.pos[2]);
  ctx.fillStyle = '#8cff9c';
  ctx.beginPath(); ctx.arc(p.x, p.y, 6, 0, Math.PI * 2); ctx.fill();
  ctx.strokeStyle = '#0c0e13'; ctx.lineWidth = 1.5; ctx.stroke();
  const yaw = (level.spawn.yaw_deg || 0) * Math.PI / 180;
  const end_wx = level.spawn.pos[0] + Math.sin(yaw) * 1.2;
  const end_wz = level.spawn.pos[2] + Math.cos(yaw) * 1.2;
  const e = world_to_screen(end_wx, end_wz);
  ctx.strokeStyle = '#8cff9c'; ctx.lineWidth = 2;
  ctx.beginPath(); ctx.moveTo(p.x, p.y); ctx.lineTo(e.x, e.y); ctx.stroke();
  draw_arrowhead(e.x, e.y, Math.atan2(e.y - p.y, e.x - p.x), '#8cff9c');
}

// -------- Opening snap helper --------------------------------------------------------

// Find the closest wall to (wx, wz), project the point onto it, snap along the wall.
// Returns { wall_idx, cx, cz, dx, dz } or null if no wall is close enough.
function snap_to_wall(wx, wz) {
  const max_perp = 1.5; // world units
  let best_perp = max_perp;
  let best = null;
  for (let i = 0; i < level.walls.length; i++) {
    const w = level.walls[i];
    const wdx = w.b[0] - w.a[0], wdz = w.b[2] - w.a[2];
    const wlen = Math.sqrt(wdx * wdx + wdz * wdz);
    if (wlen < 1e-4) continue;
    const ux = wdx / wlen, uz = wdz / wlen;
    const perp = Math.abs(-uz * (wx - w.a[0]) + ux * (wz - w.a[2]));
    if (perp >= best_perp) continue;
    const t = (wx - w.a[0]) * ux + (wz - w.a[2]) * uz;
    if (t < 0 || t > wlen) continue;
    const t_snap = editor.snap ? Math.round(t / editor.grid_step) * editor.grid_step : t;
    const t_clamped = Math.max(0, Math.min(t_snap, wlen));
    best = { wall_idx: i, cx: w.a[0] + ux * t_clamped, cz: w.a[2] + uz * t_clamped, dx: ux, dz: uz };
    best_perp = perp;
  }
  return best;
}

// -------- Opening export helper -----------------------------------------------------

// Convert world-space openings to wall-index-relative brushes for the engine JSON.
// One opening may produce brushes on several walls (e.g. both faces of a thick wall).
function compile_openings_to_brushes() {
  const brushes = [];
  const tol_perp = Math.max(editor.wall_thickness + 0.15, 0.3);
  for (const op of level.openings) {
    for (let i = 0; i < level.walls.length; i++) {
      const w = level.walls[i];
      const wdx = w.b[0] - w.a[0], wdz = w.b[2] - w.a[2];
      const wlen = Math.sqrt(wdx * wdx + wdz * wdz);
      if (wlen < 1e-4) continue;
      const wux = wdx / wlen, wuz = wdz / wlen;
      // Must be aligned with the wall direction (or its reverse)
      if (Math.abs(op.dx * wux + op.dz * wuz) < 0.9) continue;
      // Perpendicular distance from opening centre to wall line
      const perp = Math.abs(-wuz * (op.cx - w.a[0]) + wux * (op.cz - w.a[2]));
      if (perp > tol_perp) continue;
      // Along-wall distance from w.a to the opening centre
      const t = (op.cx - w.a[0]) * wux + (op.cz - w.a[2]) * wuz;
      if (t < 0 || t > wlen) continue;
      const offset = Math.max(0, Math.min(t - op.width / 2, wlen - op.width));
      brushes.push({ kind: op.kind, wall: i, offset, width: op.width, y_start: op.y_start, height: op.height });
    }
  }
  return brushes;
}

// -------- Draft drawing -------------------------------------------------------------

function draw_draft() {
  if (!editor.draft && editor.tool !== 'door' && editor.tool !== 'window') return;
  const m = editor.mouse_world;
  ctx.setLineDash([4, 4]);

  if (editor.tool === 'door' || editor.tool === 'window') {
    const snap = snap_to_wall(m.x, m.z);
    if (snap) {
      const bw = editor.tool === 'door' ? 1.2 : 1.0;
      const hx = snap.dx * bw / 2, hz = snap.dz * bw / 2;
      const p0 = world_to_screen(snap.cx - hx, snap.cz - hz);
      const p1 = world_to_screen(snap.cx + hx, snap.cz + hz);
      ctx.strokeStyle = editor.tool === 'door' ? DOOR_COLOR : WINDOW_COLOR;
      ctx.lineWidth = 4;
      ctx.setLineDash([]);
      ctx.beginPath(); ctx.moveTo(p0.x, p0.y); ctx.lineTo(p1.x, p1.y); ctx.stroke();
      // Small dot at centre
      const pc = world_to_screen(snap.cx, snap.cz);
      ctx.fillStyle = editor.tool === 'door' ? DOOR_COLOR : WINDOW_COLOR;
      ctx.beginPath(); ctx.arc(pc.x, pc.y, 3, 0, Math.PI * 2); ctx.fill();
    }
    ctx.setLineDash([]);
    return;
  }

  const d = editor.draft;
  if (d.kind === 'wall') {
    const ax = d.a[0], az = d.a[2], bx = m.x, bz = m.z;
    const a = world_to_screen(ax, az), b = world_to_screen(bx, bz);
    ctx.strokeStyle = '#ffffff'; ctx.lineWidth = 1.5;
    ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); ctx.stroke();
    const dx = bx - ax, dz = bz - az, len = Math.sqrt(dx * dx + dz * dz);
    if (len > 1e-4 && editor.wall_thickness > 0) {
      const nx = (-dz / len) * editor.wall_thickness;
      const nz = (dx / len) * editor.wall_thickness;
      const pa = world_to_screen(ax + nx, az + nz);
      const pb = world_to_screen(bx + nx, bz + nz);
      ctx.strokeStyle = 'rgba(255,255,255,0.35)';
      ctx.beginPath(); ctx.moveTo(pa.x, pa.y); ctx.lineTo(pb.x, pb.y); ctx.stroke();
    }
  } else if (d.kind === 'room') {
    const t = editor.wall_thickness;
    const min_x = Math.min(d.start.x, m.x), max_x = Math.max(d.start.x, m.x);
    const min_z = Math.min(d.start.z, m.z), max_z = Math.max(d.start.z, m.z);
    const tl = world_to_screen(min_x, min_z), br = world_to_screen(max_x, max_z);
    ctx.strokeStyle = '#ffffff'; ctx.lineWidth = 1.5;
    ctx.beginPath(); ctx.rect(tl.x, tl.y, br.x - tl.x, br.y - tl.y); ctx.stroke();
    if (max_x - min_x > t * 3 && max_z - min_z > t * 3) {
      const itl = world_to_screen(min_x + t, min_z + t);
      const ibr = world_to_screen(max_x - t, max_z - t);
      ctx.strokeStyle = 'rgba(255,255,255,0.4)';
      ctx.beginPath(); ctx.rect(itl.x, itl.y, ibr.x - itl.x, ibr.y - itl.y); ctx.stroke();
    }
  } else if (d.kind === 'sector') {
    ctx.strokeStyle = '#ffffff'; ctx.lineWidth = 1.5;
    ctx.beginPath();
    const pp0 = world_to_screen(d.points[0][0], d.points[0][1]);
    ctx.moveTo(pp0.x, pp0.y);
    for (let k = 1; k < d.points.length; ++k) {
      const p = world_to_screen(d.points[k][0], d.points[k][1]);
      ctx.lineTo(p.x, p.y);
    }
    ctx.lineTo(...Object.values(world_to_screen(m.x, m.z)));
    ctx.stroke();
    ctx.setLineDash([]);
    for (const pt of d.points) {
      const p = world_to_screen(pt[0], pt[1]);
      ctx.fillStyle = '#ffffff';
      ctx.beginPath(); ctx.arc(p.x, p.y, 3, 0, Math.PI * 2); ctx.fill();
    }
  } else if (d.kind === 'stair') {
    ctx.strokeStyle = '#ffffff'; ctx.lineWidth = 1.5;
    const a = world_to_screen(d.a[0], d.a[1]);
    const bs = world_to_screen(m.x, m.z);
    ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(bs.x, bs.y); ctx.stroke();
  }
  ctx.setLineDash([]);
}

function draw_selection_handles() {
  const sel = editor.selection;
  if (!sel) return;
  if (sel.kind === 'wall' || sel.kind === 'wall_vertex') {
    const w = level.walls[sel.idx];
    if (!w) return;
    for (const p of [w.a, w.b]) {
      const s = world_to_screen(p[0], p[2]);
      ctx.fillStyle = WALL_COLOR_SEL;
      ctx.beginPath(); ctx.arc(s.x, s.y, 5, 0, Math.PI * 2); ctx.fill();
      ctx.strokeStyle = '#0c0e13'; ctx.lineWidth = 1; ctx.stroke();
    }
  } else if (sel.kind === 'opening') {
    const op = level.openings[sel.idx];
    if (!op) return;
    const pc = world_to_screen(op.cx, op.cz);
    ctx.fillStyle = op.kind === 'window' ? WINDOW_COLOR_SEL : DOOR_COLOR_SEL;
    ctx.beginPath(); ctx.arc(pc.x, pc.y, 6, 0, Math.PI * 2); ctx.fill();
    ctx.strokeStyle = '#0c0e13'; ctx.lineWidth = 1; ctx.stroke();
  } else if (sel.kind === 'sector' || sel.kind === 'sector_vertex') {
    const s = level.sectors[sel.idx];
    if (!s) return;
    for (const p of s.polygon) {
      const sp = world_to_screen(p[0], p[1]);
      ctx.fillStyle = '#8bc4ff';
      ctx.beginPath(); ctx.arc(sp.x, sp.y, 5, 0, Math.PI * 2); ctx.fill();
      ctx.strokeStyle = '#0c0e13'; ctx.lineWidth = 1; ctx.stroke();
    }
  } else if (sel.kind === 'stair' || sel.kind === 'stair_vertex') {
    const s = level.stairs[sel.idx];
    if (!s) return;
    for (const p of [s.center_a, s.center_b]) {
      const sp = world_to_screen(p[0], p[1]);
      ctx.fillStyle = '#8bc4ff';
      ctx.beginPath(); ctx.arc(sp.x, sp.y, 5, 0, Math.PI * 2); ctx.fill();
      ctx.strokeStyle = '#0c0e13'; ctx.lineWidth = 1; ctx.stroke();
    }
  } else if (sel.kind === 'light') {
    const L = level.lights[sel.idx];
    if (!L) return;
    const sp = world_to_screen(L.pos[0], L.pos[2]);
    ctx.fillStyle = '#8bc4ff';
    ctx.beginPath(); ctx.arc(sp.x, sp.y, 7, 0, Math.PI * 2); ctx.fill();
    ctx.strokeStyle = '#0c0e13'; ctx.lineWidth = 1; ctx.stroke();
  }
}

// -------- Hit testing ----------------------------------------------------------------

function dist_point_to_segment(px, pz, ax, az, bx, bz) {
  const dx = bx - ax, dz = bz - az;
  const len_sq = dx * dx + dz * dz;
  if (len_sq < 1e-8) { return Math.sqrt((px-ax)**2 + (pz-az)**2); }
  let t = ((px - ax) * dx + (pz - az) * dz) / len_sq;
  t = Math.max(0, Math.min(1, t));
  return Math.sqrt((px - ax - dx*t)**2 + (pz - az - dz*t)**2);
}

function point_in_polygon(poly, px, pz) {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const ax = poly[i][0], az = poly[i][1], bx = poly[j][0], bz = poly[j][1];
    const crosses = (az > pz) !== (bz > pz);
    if (!crosses) continue;
    const x_at = (bx - ax) * (pz - az) / (bz - az) + ax;
    if (px < x_at) inside = !inside;
  }
  return inside;
}

function perp_dist_to_line(px, pz, ax, az, bx, bz) {
  const dx = bx - ax, dz = bz - az, len = Math.sqrt(dx*dx+dz*dz);
  if (len < 1e-6) return Infinity;
  return Math.abs((-dz/len)*(px-ax) + (dx/len)*(pz-az));
}

function hit_test(wx, wz) {
  const px_tol = 8 / editor.view.scale;
  let best = null, best_dist = Infinity;

  const check_vertex = (vx, vz, kind, idx, sub) => {
    const d = Math.sqrt((wx-vx)**2 + (wz-vz)**2);
    if (d < px_tol && d < best_dist) { best = { kind, idx, sub }; best_dist = d; }
  };

  for (let i = 0; i < level.walls.length; ++i) {
    const w = level.walls[i];
    check_vertex(w.a[0], w.a[2], 'wall_vertex', i, 'a');
    check_vertex(w.b[0], w.b[2], 'wall_vertex', i, 'b');
  }
  for (let i = 0; i < level.sectors.length; ++i) {
    const s = level.sectors[i];
    for (let k = 0; k < s.polygon.length; ++k)
      check_vertex(s.polygon[k][0], s.polygon[k][1], 'sector_vertex', i, k);
  }
  for (let i = 0; i < level.stairs.length; ++i) {
    const s = level.stairs[i];
    check_vertex(s.center_a[0], s.center_a[1], 'stair_vertex', i, 'a');
    check_vertex(s.center_b[0], s.center_b[1], 'stair_vertex', i, 'b');
  }
  for (let i = 0; i < level.lights.length; ++i)
    check_vertex(level.lights[i].pos[0], level.lights[i].pos[2], 'light_vertex', i, null);

  if (best) return best;

  // Opening centres
  for (let i = 0; i < level.openings.length; ++i) {
    const op = level.openings[i];
    const hx = op.dx * op.width / 2, hz = op.dz * op.width / 2;
    const d = dist_point_to_segment(wx, wz, op.cx-hx, op.cz-hz, op.cx+hx, op.cz+hz);
    if (d < px_tol && d < best_dist) { best = { kind: 'opening', idx: i }; best_dist = d; }
  }
  if (best) return best;

  // Wall segments
  for (let i = 0; i < level.walls.length; ++i) {
    const w = level.walls[i];
    const d = dist_point_to_segment(wx, wz, w.a[0], w.a[2], w.b[0], w.b[2]);
    if (d < px_tol && d < best_dist) { best = { kind: 'wall', idx: i }; best_dist = d; }
  }
  if (best) return best;

  for (let i = 0; i < level.sectors.length; ++i) {
    if (point_in_polygon(level.sectors[i].polygon, wx, wz)) return { kind: 'sector', idx: i };
  }
  return null;
}

// -------- Wall helpers ---------------------------------------------------------------

function wall_exists(ax, az, bx, bz) {
  const tol = 0.05;
  for (const w of level.walls) {
    if ((Math.abs(w.a[0]-ax)<tol && Math.abs(w.a[2]-az)<tol && Math.abs(w.b[0]-bx)<tol && Math.abs(w.b[2]-bz)<tol) ||
        (Math.abs(w.a[0]-bx)<tol && Math.abs(w.a[2]-bz)<tol && Math.abs(w.b[0]-ax)<tol && Math.abs(w.b[2]-az)<tol))
      return true;
  }
  return false;
}

function finalize_room(start, end) {
  const t = editor.wall_thickness;
  const y = editor.default_floor_y;
  const h = editor.default_ceiling_y - y;
  const min_x = Math.min(start.x, end.x), max_x = Math.max(start.x, end.x);
  const min_z = Math.min(start.z, end.z), max_z = Math.max(start.z, end.z);
  if ((max_x - min_x) < t * 2 + 0.5 || (max_z - min_z) < t * 2 + 0.5) return;

  const outer_edges = [
    { a: [min_x, y, min_z], b: [max_x, y, min_z] }, // top    (+X)
    { a: [max_x, y, min_z], b: [max_x, y, max_z] }, // right  (+Z)
    { a: [max_x, y, max_z], b: [min_x, y, max_z] }, // bottom (-X)
    { a: [min_x, y, max_z], b: [min_x, y, min_z] }, // left   (-Z)
  ];
  const inner_edges = [
    { a: [min_x, y, min_z + t], b: [max_x, y, min_z + t] }, // top inner    (+X)
    { a: [max_x - t, y, min_z], b: [max_x - t, y, max_z] }, // right inner  (+Z)
    { a: [max_x, y, max_z - t], b: [min_x, y, max_z - t] }, // bottom inner (-X)
    { a: [min_x + t, y, max_z], b: [min_x + t, y, min_z] }, // left inner   (-Z)
  ];

  const added_outer = new Array(4).fill(-1);
  const added_inner = new Array(4).fill(-1);
  for (let i = 0; i < 4; i++) {
    const e = outer_edges[i];
    if (!wall_exists(e.a[0], e.a[2], e.b[0], e.b[2])) {
      added_outer[i] = level.walls.length;
      level.walls.push({ a: e.a, b: e.b, height: h });
    }
  }
  for (let i = 0; i < 4; i++) {
    const e = inner_edges[i];
    if (!wall_exists(e.a[0], e.a[2], e.b[0], e.b[2])) {
      added_inner[i] = level.walls.length;
      level.walls.push({ a: e.a, b: e.b, height: h });
    }
  }
  for (let i = 0; i < 4; i++) {
    if (added_outer[i] >= 0 && added_inner[i] >= 0)
      editor.wall_pairs.push([added_outer[i], added_inner[i]]);
  }

  level.sectors.push({
    id: `sector_${level.sectors.length}`,
    polygon: [[min_x, min_z],[max_x, min_z],[max_x, max_z],[min_x, max_z]],
    floor_y: y,
    ceiling_y: editor.default_ceiling_y,
  });
}

// -------- Mouse & keyboard -----------------------------------------------------------

function canvas_event_pos(ev) {
  const rect = canvas.getBoundingClientRect();
  return { x: ev.clientX - rect.left, y: ev.clientY - rect.top };
}

canvas.addEventListener('mousemove', (ev) => {
  const p = canvas_event_pos(ev);
  editor.mouse_screen = p;
  let w = screen_to_world(p.x, p.y);
  w = snap_world(w);
  editor.mouse_world = { x: w.x, z: w.z };
  if (editor.panning && editor.pan_start) {
    editor.view.ox = editor.pan_start.ox + (p.x - editor.pan_start.sx);
    editor.view.oy = editor.pan_start.oy + (p.y - editor.pan_start.sy);
  }
  if (editor.tool === 'select' && editor.dragging && editor.selection) apply_drag(w);
  render();
});

canvas.addEventListener('mousedown', (ev) => {
  const p = canvas_event_pos(ev);
  if (ev.button === 1 || (ev.button === 0 && editor.space_held)) {
    editor.panning = true;
    editor.pan_start = { sx: p.x, sy: p.y, ox: editor.view.ox, oy: editor.view.oy };
    return;
  }
  if (ev.button !== 0) return;
  const w = editor.mouse_world;

  switch (editor.tool) {
    case 'select': {
      const hit = hit_test(w.x, w.z);
      editor.selection = normalize_hit(hit);
      editor.dragging = !!editor.selection;
      refresh_selection_panel();
      break;
    }
    case 'wall': {
      if (!editor.draft) {
        editor.draft = { kind: 'wall', a: [w.x, editor.default_floor_y, w.z] };
      } else {
        const a = editor.draft.a, b = [w.x, editor.default_floor_y, w.z];
        const h = editor.default_ceiling_y - editor.default_floor_y;
        let placed_idx = -1;
        if (!wall_exists(a[0], a[2], b[0], b[2])) {
          level.walls.push({ a, b, height: h });
          placed_idx = level.walls.length - 1;
          if (editor.wall_thickness > 0) {
            const dx = b[0]-a[0], dz = b[2]-a[2], len = Math.sqrt(dx*dx+dz*dz);
            if (len > 1e-4) {
              const nx = (-dz/len)*editor.wall_thickness, nz = (dx/len)*editor.wall_thickness;
              const pa = [a[0]+nx, a[1], a[2]+nz], pb = [b[0]+nx, b[1], b[2]+nz];
              if (!wall_exists(pa[0], pa[2], pb[0], pb[2])) {
                editor.wall_pairs.push([placed_idx, level.walls.length]);
                level.walls.push({ a: pa, b: pb, height: h });
              }
            }
          }
        }
        editor.selection = placed_idx >= 0 ? { kind: 'wall', idx: placed_idx } : null;
        editor.draft = null;
        refresh_selection_panel();
      }
      break;
    }
    case 'door':
    case 'window': {
      const snap = snap_to_wall(w.x, w.z);
      if (snap) {
        const is_window = editor.tool === 'window';
        level.openings.push({
          kind: editor.tool,
          cx: snap.cx, cz: snap.cz,
          dx: snap.dx, dz: snap.dz,
          width:   is_window ? 1.0 : 1.2,
          height:  is_window ? 0.9 : 2.2,
          y_start: is_window ? editor.default_floor_y + 1.2 : editor.default_floor_y,
        });
        editor.selection = { kind: 'opening', idx: level.openings.length - 1 };
        refresh_selection_panel();
      }
      break;
    }
    case 'room': {
      editor.draft = { kind: 'room', start: { x: w.x, z: w.z } };
      break;
    }
    case 'sector': {
      if (!editor.draft) editor.draft = { kind: 'sector', points: [[w.x, w.z]] };
      else editor.draft.points.push([w.x, w.z]);
      break;
    }
    case 'stair': {
      if (!editor.draft) {
        editor.draft = { kind: 'stair', a: [w.x, w.z] };
      } else {
        level.stairs.push({
          center_a: editor.draft.a, center_b: [w.x, w.z],
          width: 2.0, from_y: editor.default_floor_y,
          to_y: editor.default_floor_y + editor.default_ceiling_y, steps: 8,
        });
        editor.draft = null;
        editor.selection = { kind: 'stair', idx: level.stairs.length - 1 };
        refresh_selection_panel();
      }
      break;
    }
    case 'light': {
      level.lights.push({ pos: [w.x, editor.default_ceiling_y - 0.6, w.z], color: [2.4, 2.1, 1.7], intensity: 1.0 });
      editor.selection = { kind: 'light', idx: level.lights.length - 1 };
      refresh_selection_panel();
      break;
    }
    case 'spawn': {
      level.spawn.pos = [w.x, editor.default_floor_y, w.z];
      editor.selection = { kind: 'spawn' };
      refresh_selection_panel();
      break;
    }
    case 'erase': {
      const hit = hit_test(w.x, w.z);
      if (hit) delete_hit(hit);
      break;
    }
  }
  refresh_counts();
  render();
});

canvas.addEventListener('mouseup', () => {
  editor.panning = false;
  editor.dragging = false;
  if (editor.tool === 'room' && editor.draft?.kind === 'room') {
    finalize_room(editor.draft.start, editor.mouse_world);
    editor.draft = null;
    refresh_counts();
    render();
  }
});

canvas.addEventListener('wheel', (ev) => {
  ev.preventDefault();
  const p = canvas_event_pos(ev);
  const wb = screen_to_world(p.x, p.y);
  editor.view.scale = Math.max(4, Math.min(300, editor.view.scale * (ev.deltaY < 0 ? 1.15 : 1/1.15)));
  const wa = screen_to_world(p.x, p.y);
  editor.view.ox += (wa.x - wb.x) * editor.view.scale;
  editor.view.oy += (wa.z - wb.z) * editor.view.scale;
  render();
}, { passive: false });

window.addEventListener('keydown', (ev) => {
  if (ev.target.tagName === 'INPUT') return;
  if (ev.code === 'Space') { editor.space_held = true; ev.preventDefault(); return; }
  if (ev.key === 'Escape') {
    editor.draft = null; editor.selection = null;
    refresh_selection_panel(); render(); return;
  }
  if (ev.key === 'Enter' && editor.draft?.kind === 'sector') {
    if (editor.draft.points.length >= 3) {
      level.sectors.push({ id: `sector_${level.sectors.length}`, polygon: editor.draft.points,
        floor_y: editor.default_floor_y, ceiling_y: editor.default_ceiling_y });
      editor.draft = null; refresh_counts(); render();
    }
    return;
  }
  if ((ev.key === 'Backspace' || ev.key === 'Delete') && editor.selection) {
    delete_selection(); refresh_counts(); refresh_selection_panel(); render();
  }
});
window.addEventListener('keyup', (ev) => { if (ev.code === 'Space') editor.space_held = false; });

// -------- Selection ------------------------------------------------------------------

function normalize_hit(hit) {
  if (!hit) return null;
  if (hit.kind === 'wall_vertex')   return { kind: 'wall_vertex',   idx: hit.idx, sub: hit.sub };
  if (hit.kind === 'sector_vertex') return { kind: 'sector_vertex', idx: hit.idx, sub: hit.sub };
  if (hit.kind === 'stair_vertex')  return { kind: 'stair_vertex',  idx: hit.idx, sub: hit.sub };
  if (hit.kind === 'light_vertex')  return { kind: 'light',          idx: hit.idx };
  return hit;
}

function apply_drag(w) {
  const sel = editor.selection;
  if (!sel) return;
  const snapped = snap_world(w);

  if (sel.kind === 'wall_vertex') {
    const wall = level.walls[sel.idx];
    const prev_x = sel.sub === 'a' ? wall.a[0] : wall.b[0];
    const prev_z = sel.sub === 'a' ? wall.a[2] : wall.b[2];
    if (sel.sub === 'a') wall.a = [snapped.x, wall.a[1], snapped.z];
    else                 wall.b = [snapped.x, wall.b[1], snapped.z];
    const dx = snapped.x - prev_x, dz = snapped.z - prev_z;
    if (dx === 0 && dz === 0) return;
    // Move paired wall's corresponding vertex
    const partner_idx = find_pair_partner(sel.idx);
    if (partner_idx >= 0) {
      const partner = level.walls[partner_idx];
      const psub = pair_partner_sub(wall, partner, sel.sub);
      if (psub === 'a') { partner.a[0] += dx; partner.a[2] += dz; }
      else              { partner.b[0] += dx; partner.b[2] += dz; }
    }
    // Vertex weld: move any coincident wall endpoints by the same delta
    const tol = 0.05;
    const moved = new Set([sel.idx, partner_idx]);
    for (let i = 0; i < level.walls.length; i++) {
      if (moved.has(i)) continue;
      const wi = level.walls[i];
      const pi = find_pair_partner(i);
      for (const sub of ['a', 'b']) {
        if (Math.abs(wi[sub][0]-prev_x)<tol && Math.abs(wi[sub][2]-prev_z)<tol) {
          wi[sub][0] += dx; wi[sub][2] += dz;
          moved.add(i);
          if (pi >= 0 && !moved.has(pi)) {
            const pw = level.walls[pi];
            const psub2 = pair_partner_sub(wi, pw, sub);
            pw[psub2][0] += dx; pw[psub2][2] += dz;
            moved.add(pi);
          }
          break;
        }
      }
    }
  } else if (sel.kind === 'opening') {
    const op = level.openings[sel.idx];
    op.cx = snapped.x; op.cz = snapped.z;
  } else if (sel.kind === 'sector_vertex') {
    level.sectors[sel.idx].polygon[sel.sub] = [snapped.x, snapped.z];
  } else if (sel.kind === 'stair_vertex') {
    const s = level.stairs[sel.idx];
    if (sel.sub === 'a') s.center_a = [snapped.x, snapped.z];
    else                 s.center_b = [snapped.x, snapped.z];
  } else if (sel.kind === 'light') {
    level.lights[sel.idx].pos[0] = snapped.x;
    level.lights[sel.idx].pos[2] = snapped.z;
  } else if (sel.kind === 'spawn') {
    level.spawn.pos[0] = snapped.x;
    level.spawn.pos[2] = snapped.z;
  }
}

function delete_selection() {
  const sel = editor.selection;
  if (!sel) return;
  if (sel.kind === 'wall' || sel.kind === 'wall_vertex') {
    const partner = find_pair_partner(sel.idx);
    delete_walls_at_indices(partner >= 0 ? [sel.idx, partner] : [sel.idx]);
  } else if (sel.kind === 'opening') {
    level.openings.splice(sel.idx, 1);
  } else if (sel.kind === 'sector' || sel.kind === 'sector_vertex') {
    level.sectors.splice(sel.idx, 1);
  } else if (sel.kind === 'stair' || sel.kind === 'stair_vertex') {
    level.stairs.splice(sel.idx, 1);
  } else if (sel.kind === 'light') {
    level.lights.splice(sel.idx, 1);
  }
  editor.selection = null;
}

function delete_hit(hit) {
  editor.selection = normalize_hit(hit);
  delete_selection();
  refresh_selection_panel();
}

function refresh_selection_panel() {
  const info = document.getElementById('selection-info');
  const sel = editor.selection;
  if (!sel) { info.textContent = '(nothing selected)'; return; }
  if (sel.kind === 'wall' || sel.kind === 'wall_vertex') {
    info.innerHTML = render_wall_edit(sel.idx, level.walls[sel.idx]);
    wire_wall_edit(sel.idx);
  } else if (sel.kind === 'opening') {
    info.innerHTML = render_opening_edit(sel.idx, level.openings[sel.idx]);
    wire_opening_edit(sel.idx);
  } else if (sel.kind === 'sector' || sel.kind === 'sector_vertex') {
    info.innerHTML = render_sector_edit(sel.idx, level.sectors[sel.idx]);
    wire_sector_edit(sel.idx);
  } else if (sel.kind === 'stair' || sel.kind === 'stair_vertex') {
    info.innerHTML = render_stair_edit(sel.idx, level.stairs[sel.idx]);
    wire_stair_edit(sel.idx);
  } else if (sel.kind === 'light') {
    info.innerHTML = render_light_edit(sel.idx, level.lights[sel.idx]);
    wire_light_edit(sel.idx);
  } else if (sel.kind === 'spawn') {
    info.innerHTML = render_spawn_edit();
    wire_spawn_edit();
  }
}

function field(id, label, value, step) {
  return `<label>${label} <input id="${id}" type="number" step="${step || 0.1}" value="${value}" /></label>`;
}

function render_wall_edit(i, w) {
  const wall_len = Math.sqrt((w.b[0]-w.a[0])**2 + (w.b[2]-w.a[2])**2).toFixed(2);
  return `
    <div>wall ${i} &middot; len ${wall_len}m</div>
    ${field('f-wall-ay', 'base y', w.a[1])}
    ${field('f-wall-h', 'height', w.height)}
    <div class="btn-row">
      <button id="f-wall-add-door" class="primary">add door</button>
      <button id="f-wall-add-window">add window</button>
    </div>
  `;
}

function wire_wall_edit(i) {
  const w = level.walls[i];
  document.getElementById('f-wall-ay').oninput = (e) => { const y=parseFloat(e.target.value); w.a[1]=y; w.b[1]=y; render(); };
  document.getElementById('f-wall-h').oninput  = (e) => { w.height=parseFloat(e.target.value); render(); };
  document.getElementById('f-wall-add-door').onclick = () => {
    const cx = (w.a[0]+w.b[0])/2, cz = (w.a[2]+w.b[2])/2;
    const dx = w.b[0]-w.a[0], dz = w.b[2]-w.a[2], len = Math.sqrt(dx*dx+dz*dz);
    level.openings.push({ kind:'door', cx, cz, dx:dx/len, dz:dz/len, width:1.2, height:2.2, y_start:w.a[1] });
    editor.selection = { kind:'opening', idx: level.openings.length-1 };
    refresh_selection_panel(); refresh_counts(); render();
  };
  document.getElementById('f-wall-add-window').onclick = () => {
    const cx = (w.a[0]+w.b[0])/2, cz = (w.a[2]+w.b[2])/2;
    const dx = w.b[0]-w.a[0], dz = w.b[2]-w.a[2], len = Math.sqrt(dx*dx+dz*dz);
    level.openings.push({ kind:'window', cx, cz, dx:dx/len, dz:dz/len, width:1.0, height:0.9, y_start:w.a[1]+1.2 });
    editor.selection = { kind:'opening', idx: level.openings.length-1 };
    refresh_selection_panel(); refresh_counts(); render();
  };
}

function render_opening_edit(i, op) {
  return `
    <div>opening ${i} &middot; ${op.kind}</div>
    ${field('f-op-cx', 'center x', op.cx.toFixed(3))}
    ${field('f-op-cz', 'center z', op.cz.toFixed(3))}
    ${field('f-op-w', 'width', op.width)}
    ${field('f-op-h', 'height', op.height)}
    ${field('f-op-ys', 'y_start', op.y_start)}
  `;
}

function wire_opening_edit(i) {
  const op = level.openings[i];
  document.getElementById('f-op-cx').oninput  = (e) => { op.cx     = parseFloat(e.target.value); render(); };
  document.getElementById('f-op-cz').oninput  = (e) => { op.cz     = parseFloat(e.target.value); render(); };
  document.getElementById('f-op-w').oninput   = (e) => { op.width  = parseFloat(e.target.value); render(); };
  document.getElementById('f-op-h').oninput   = (e) => { op.height = parseFloat(e.target.value); render(); };
  document.getElementById('f-op-ys').oninput  = (e) => { op.y_start= parseFloat(e.target.value); render(); };
}

function render_sector_edit(i, s) {
  return `
    <div>sector ${i} (${s.polygon.length} verts)</div>
    <label>id <input id="f-sec-id" type="text" value="${s.id || ''}" /></label>
    ${field('f-sec-fy', 'floor_y', s.floor_y)}
    ${field('f-sec-cy', 'ceiling_y', s.ceiling_y)}
  `;
}
function wire_sector_edit(i) {
  const s = level.sectors[i];
  document.getElementById('f-sec-id').oninput = (e) => { s.id = e.target.value; render(); };
  document.getElementById('f-sec-fy').oninput = (e) => { s.floor_y   = parseFloat(e.target.value); render(); };
  document.getElementById('f-sec-cy').oninput = (e) => { s.ceiling_y = parseFloat(e.target.value); render(); };
}

function render_stair_edit(i, s) {
  return `<div>stair ${i}</div>
    ${field('f-stair-w','width',s.width)} ${field('f-stair-fy','from_y',s.from_y)}
    ${field('f-stair-ty','to_y',s.to_y)} ${field('f-stair-steps','steps',s.steps,'1')}`;
}
function wire_stair_edit(i) {
  const s = level.stairs[i];
  document.getElementById('f-stair-w').oninput     = (e) => { s.width  = parseFloat(e.target.value); render(); };
  document.getElementById('f-stair-fy').oninput    = (e) => { s.from_y = parseFloat(e.target.value); render(); };
  document.getElementById('f-stair-ty').oninput    = (e) => { s.to_y   = parseFloat(e.target.value); render(); };
  document.getElementById('f-stair-steps').oninput = (e) => { s.steps  = parseInt(e.target.value,10); render(); };
}

function render_light_edit(i, L) {
  return `<div>light ${i}</div>
    ${field('f-light-y','pos.y',L.pos[1])} ${field('f-light-i','intensity',L.intensity,'0.05')}
    ${field('f-light-r','color.r',L.color[0])} ${field('f-light-g','color.g',L.color[1])} ${field('f-light-b','color.b',L.color[2])}`;
}
function wire_light_edit(i) {
  const L = level.lights[i];
  document.getElementById('f-light-y').oninput = (e) => { L.pos[1]    = parseFloat(e.target.value); render(); };
  document.getElementById('f-light-i').oninput = (e) => { L.intensity = parseFloat(e.target.value); render(); };
  document.getElementById('f-light-r').oninput = (e) => { L.color[0]  = parseFloat(e.target.value); render(); };
  document.getElementById('f-light-g').oninput = (e) => { L.color[1]  = parseFloat(e.target.value); render(); };
  document.getElementById('f-light-b').oninput = (e) => { L.color[2]  = parseFloat(e.target.value); render(); };
}

function render_spawn_edit() {
  return `<div>spawn</div>
    ${field('f-spawn-y','pos.y',level.spawn.pos[1])} ${field('f-spawn-yaw','yaw_deg',level.spawn.yaw_deg,'5')}`;
}
function wire_spawn_edit() {
  document.getElementById('f-spawn-y').oninput   = (e) => { level.spawn.pos[1]   = parseFloat(e.target.value); render(); };
  document.getElementById('f-spawn-yaw').oninput = (e) => { level.spawn.yaw_deg  = parseFloat(e.target.value); render(); };
}

// -------- Counts ---------------------------------------------------------------------

function refresh_counts() {
  document.getElementById('counts').innerHTML =
    `<div>sectors</div><div>${level.sectors.length}</div>` +
    `<div>walls</div><div>${level.walls.length}</div>` +
    `<div>openings</div><div>${level.openings.length}</div>` +
    `<div>stairs</div><div>${level.stairs.length}</div>` +
    `<div>lights</div><div>${level.lights.length}</div>`;
}

// -------- Tool UI --------------------------------------------------------------------

const TOOL_HINTS = {
  select: 'click to select. drag handles to move. delete to remove.',
  wall:   'click two points to place a wall (auto-adds parallel inner face). esc = cancel.',
  door:   'move near a wall — opening snaps to it. click to place.',
  window: 'move near a wall — opening snaps to it. click to place.',
  room:   'drag to build a rectangular room with thick walls and sector.',
  sector: 'click points to outline a polygon. enter to close. esc = cancel.',
  stair:  'click bottom center then top center.',
  light:  'click to place a light.',
  spawn:  'click to set player spawn.',
  erase:  'click anything to delete it.',
};

document.querySelectorAll('.tool-palette button').forEach((btn) => {
  btn.onclick = () => {
    editor.tool = btn.dataset.tool;
    editor.draft = null;
    document.querySelectorAll('.tool-palette button').forEach((b) => b.classList.toggle('active', b === btn));
    document.getElementById('tool-hint').textContent = TOOL_HINTS[editor.tool] || '';
    render();
  };
});
document.querySelector('.tool-palette button[data-tool="select"]').classList.add('active');
document.getElementById('tool-hint').textContent = TOOL_HINTS.select;

// -------- Meta fields ----------------------------------------------------------------

document.getElementById('level-name').oninput        = (e) => { level.name         = e.target.value; };
document.getElementById('level-wall-height').oninput  = (e) => { level.wall_height  = parseFloat(e.target.value); };
document.getElementById('grid-step').oninput          = (e) => { editor.grid_step   = parseFloat(e.target.value); render(); };
document.getElementById('snap').onchange              = (e) => { editor.snap        = e.target.checked; };
document.getElementById('default-floor-y').oninput    = (e) => { editor.default_floor_y   = parseFloat(e.target.value); };
document.getElementById('default-ceiling-y').oninput  = (e) => { editor.default_ceiling_y = parseFloat(e.target.value); };
document.getElementById('wall-thickness').oninput     = (e) => { editor.wall_thickness     = parseFloat(e.target.value); };

// -------- Export / import ------------------------------------------------------------

document.getElementById('export-json').onclick = () => {
  const brushes = compile_openings_to_brushes();
  const out = {
    version: 3, name: level.name, wall_height: level.wall_height,
    ambient: level.ambient, spawn: level.spawn,
    sectors: level.sectors, walls: level.walls,
    brushes: brushes.map(({ kind, ...rest }) => kind ? { kind, ...rest } : rest),
    stairs: level.stairs, lights: level.lights,
  };
  const blob = new Blob([JSON.stringify(out, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url; a.download = (level.name || 'untitled') + '.json'; a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
};

document.getElementById('import-json').onclick = () => document.getElementById('import-file').click();
document.getElementById('import-file').onchange = async (ev) => {
  const file = ev.target.files[0];
  if (!file) return;
  import_level(await file.text());
};
document.getElementById('load-mansion').onclick = async () => {
  try { import_level(await (await fetch('../assets/maps/mansion.json')).text()); }
  catch (e) { alert('Failed to load mansion.json: ' + e); }
};

function import_level(text) {
  let obj;
  try { obj = JSON.parse(text); } catch (e) { alert('json parse error: ' + e); return; }
  if (obj.version !== 3) { alert('expected version 3 (got ' + obj.version + ')'); return; }

  editor.wall_pairs = [];
  level.name        = obj.name || '';
  level.wall_height = obj.wall_height ?? 3.2;
  level.ambient     = obj.ambient ?? [0.07, 0.08, 0.11];
  level.spawn       = obj.spawn ?? { pos: [0,0,0], yaw_deg: 0 };
  level.sectors     = obj.sectors ?? [];
  level.walls = (obj.walls ?? []).map((w) => ({
    a: w.a ?? [0,0,0], b: w.b ?? [1,0,0], height: w.height ?? (obj.wall_height ?? 3.2),
  }));
  level.stairs = (obj.stairs ?? []).map((s) => ({
    center_a: s.center_a, center_b: s.center_b,
    width: s.width ?? 2, from_y: s.from_y ?? 0, to_y: s.to_y ?? 3.2, steps: s.steps ?? 8,
  }));
  level.lights = (obj.lights ?? []).map((L) => ({
    pos: L.pos, color: L.color ?? [2.4,2.1,1.7], intensity: L.intensity ?? 1.0,
  }));

  // Convert wall-relative brushes to world-space openings (deduplicated by position).
  const raw_openings = [];
  for (const br of obj.brushes ?? []) {
    const w = level.walls[br.wall];
    if (!w) continue;
    const dx = w.b[0]-w.a[0], dz = w.b[2]-w.a[2];
    const len = Math.sqrt(dx*dx+dz*dz);
    if (len < 1e-4) continue;
    const ux = dx/len, uz = dz/len;
    const t_center = (br.offset ?? 0) + (br.width ?? 1.2) / 2;
    const cx = w.a[0] + ux * t_center, cz = w.a[2] + uz * t_center;
    // Deduplicate openings that are very close (parallel wall pairs produce two brushes)
    const dup = raw_openings.some(op => Math.abs(op.cx-cx)<0.25 && Math.abs(op.cz-cz)<0.25);
    if (!dup) raw_openings.push({ kind: br.kind ?? 'door', cx, cz, dx: ux, dz: uz,
      width: br.width ?? 1.2, height: br.height ?? 2.2, y_start: br.y_start ?? 0 });
  }
  level.openings = raw_openings;

  document.getElementById('level-name').value        = level.name;
  document.getElementById('level-wall-height').value = level.wall_height;
  frame_to_content();
  refresh_counts();
  refresh_selection_panel();
  render();
}

function frame_to_content() {
  const pts = [];
  for (const s of level.sectors) for (const p of s.polygon) pts.push([p[0], p[1]]);
  for (const w of level.walls) { pts.push([w.a[0], w.a[2]]); pts.push([w.b[0], w.b[2]]); }
  for (const s of level.stairs) { pts.push(s.center_a); pts.push(s.center_b); }
  if (!pts.length) return;
  let minx=Infinity, maxx=-Infinity, minz=Infinity, maxz=-Infinity;
  for (const p of pts) { minx=Math.min(minx,p[0]); maxx=Math.max(maxx,p[0]); minz=Math.min(minz,p[1]); maxz=Math.max(maxz,p[1]); }
  const w = canvas.clientWidth, h = canvas.clientHeight, pad = 40;
  editor.view.scale = Math.max(4, Math.min((w-pad*2)/Math.max(maxx-minx,1), (h-pad*2)/Math.max(maxz-minz,1), 60));
  editor.view.ox = pad - minx * editor.view.scale;
  editor.view.oy = pad - minz * editor.view.scale;
}

// -------- Init -----------------------------------------------------------------------

refresh_counts();
refresh_selection_panel();
resize_canvas();
