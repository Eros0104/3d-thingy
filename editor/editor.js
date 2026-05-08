// fps-engine level editor — drag room tool
// Single-file vanilla JS; no build step. Writes v3 JSON matching src/json_level.cpp.

const WALL_COLOR     = '#78a4d6';
const WALL_COLOR_SEL = '#8bc4ff';

const level = {
  version: 3,
  name: 'untitled',
  wall_height: 3.2,
  ambient: [0.07, 0.08, 0.11],
  spawn: { pos: [0, 0, 0], yaw_deg: 0 },
  sectors: [],
  walls: [],
  stairs: [],
  lights: [],
};

// Editor-side room registry. Walls and sectors carry roomId; rooms[] is source of truth for bounds.
const rooms = [];

const editor = {
  tool: 'select',
  default_floor_y: 0.0,
  default_ceiling_y: 3.2,
  snap: true,
  grid_step: 0.5,
  view: { ox: 600, oy: 400, scale: 20 },
  draft: null,
  selection: null,
  drag_anchor: null,
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

// -------- Room management ------------------------------------------------------------

let _room_counter = 0;
function new_room_id() { return `room_${Date.now()}_${_room_counter++}`; }

function get_room_walls(room) {
  return level.walls.map((w, i) => ({ w, i })).filter(({ w }) => w.roomId === room.id);
}

function get_room_sector_idx(room) {
  return level.sectors.findIndex(s => s.roomId === room.id);
}

// Walls wind so their left-hand normal (CCW) faces INTO the room interior.
// N: dir +X → normal +Z (inward), E: dir +Z → normal -X, S: dir -X → normal -Z, W: dir -Z → normal +X
function room_wall_geom(min_x, min_z, max_x, max_z, y) {
  return [
    { a: [min_x, y, min_z], b: [max_x, y, min_z] }, // North
    { a: [max_x, y, min_z], b: [max_x, y, max_z] }, // East
    { a: [max_x, y, max_z], b: [min_x, y, max_z] }, // South
    { a: [min_x, y, max_z], b: [min_x, y, min_z] }, // West
  ];
}

function update_room_geometry(room) {
  const { min_x, min_z, max_x, max_z, floor_y, ceiling_y } = room;
  const h = ceiling_y - floor_y;
  const geom = room_wall_geom(min_x, min_z, max_x, max_z, floor_y);
  const rw = get_room_walls(room);
  for (let i = 0; i < 4 && i < rw.length; i++) {
    rw[i].w.a = [...geom[i].a];
    rw[i].w.b = [...geom[i].b];
    rw[i].w.height = h;
  }
  const si = get_room_sector_idx(room);
  if (si >= 0) {
    level.sectors[si].polygon = [[min_x, min_z], [max_x, min_z], [max_x, max_z], [min_x, max_z]];
    level.sectors[si].floor_y = floor_y;
    level.sectors[si].ceiling_y = ceiling_y;
  }
}

function create_room(min_x, min_z, max_x, max_z) {
  if (max_x - min_x < 0.3 || max_z - min_z < 0.3) return -1;
  const floor_y = editor.default_floor_y;
  const ceiling_y = editor.default_ceiling_y;
  const h = ceiling_y - floor_y;
  const id = new_room_id();
  for (const g of room_wall_geom(min_x, min_z, max_x, max_z, floor_y)) {
    level.walls.push({ a: [...g.a], b: [...g.b], height: h, roomId: id });
  }
  level.sectors.push({
    id: `sector_${level.sectors.length}`,
    polygon: [[min_x, min_z], [max_x, min_z], [max_x, max_z], [min_x, max_z]],
    floor_y,
    ceiling_y,
    roomId: id,
  });
  rooms.push({ id, min_x, min_z, max_x, max_z, floor_y, ceiling_y });
  return rooms.length - 1;
}

function resize_room_corner(room, corner, wx, wz) {
  switch (corner) {
    case 'TL': room.min_x = wx; room.min_z = wz; break;
    case 'TR': room.max_x = wx; room.min_z = wz; break;
    case 'BR': room.max_x = wx; room.max_z = wz; break;
    case 'BL': room.min_x = wx; room.max_z = wz; break;
  }
  if (room.min_x > room.max_x) [room.min_x, room.max_x] = [room.max_x, room.min_x];
  if (room.min_z > room.max_z) [room.min_z, room.max_z] = [room.max_z, room.min_z];
  update_room_geometry(room);
}

function delete_room(idx) {
  const room = rooms[idx];
  if (!room) return;
  for (let i = level.walls.length - 1; i >= 0; i--)
    if (level.walls[i].roomId === room.id) level.walls.splice(i, 1);
  for (let i = level.sectors.length - 1; i >= 0; i--)
    if (level.sectors[i].roomId === room.id) level.sectors.splice(i, 1);
  rooms.splice(idx, 1);
}

// -------- Rendering ------------------------------------------------------------------

function render() {
  const w = canvas.clientWidth;
  const h = canvas.clientHeight;
  ctx.clearRect(0, 0, w, h);
  draw_grid(w, h);
  draw_sectors();
  draw_walls();
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
  const sel = editor.selection;
  for (let i = 0; i < level.sectors.length; i++) {
    const s = level.sectors[i];
    if (s.polygon.length < 3) continue;
    const is_sel_room = sel?.kind === 'room' && rooms[sel.idx]?.id === s.roomId;
    ctx.beginPath();
    const p0 = world_to_screen(s.polygon[0][0], s.polygon[0][1]);
    ctx.moveTo(p0.x, p0.y);
    for (let k = 1; k < s.polygon.length; k++) {
      const p = world_to_screen(s.polygon[k][0], s.polygon[k][1]);
      ctx.lineTo(p.x, p.y);
    }
    ctx.closePath();
    ctx.fillStyle = is_sel_room
      ? 'rgba(139,196,255,0.12)'
      : `rgba(58,63,79,${Math.min(0.55, 0.25 + s.floor_y * 0.05)})`;
    ctx.fill();
    ctx.strokeStyle = is_sel_room ? 'rgba(139,196,255,0.35)' : '#555b73';
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

function draw_walls() {
  const sel = editor.selection;
  const sel_room_id = sel?.kind === 'room' ? rooms[sel.idx]?.id : null;
  for (let i = 0; i < level.walls.length; i++) {
    const wall = level.walls[i];
    const a = world_to_screen(wall.a[0], wall.a[2]);
    const b = world_to_screen(wall.b[0], wall.b[2]);
    const highlighted = sel_room_id && wall.roomId === sel_room_id;
    ctx.strokeStyle = highlighted ? WALL_COLOR_SEL : WALL_COLOR;
    ctx.lineWidth = highlighted ? 3 : 2;
    ctx.lineCap = 'square';
    ctx.beginPath();
    ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y);
    ctx.stroke();
  }
  ctx.lineCap = 'butt';
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
    for (let k = 1; k < 4; k++) {
      const p = world_to_screen(corners[k][0], corners[k][1]);
      ctx.lineTo(p.x, p.y);
    }
    ctx.closePath();
    ctx.fillStyle = 'rgba(232,184,78,0.2)';
    ctx.fill();
    ctx.strokeStyle = '#e8b84e';
    ctx.lineWidth = 1.5;
    ctx.stroke();
    ctx.lineWidth = 0.5;
    ctx.beginPath();
    for (let i = 1; i < s.steps; i++) {
      const t = i / s.steps;
      const mx = s.center_a[0] + dx * t, mz = s.center_a[1] + dz * t;
      const a = world_to_screen(mx + nx * hw, mz + nz * hw);
      const b = world_to_screen(mx - nx * hw, mz - nz * hw);
      ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y);
    }
    ctx.stroke();
    const startS = world_to_screen(s.center_a[0], s.center_a[1]);
    const endS   = world_to_screen(s.center_b[0], s.center_b[1]);
    ctx.strokeStyle = '#e8b84e'; ctx.lineWidth = 2;
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

function draw_draft() {
  const d = editor.draft;
  if (!d) return;
  ctx.setLineDash([4, 4]);
  if (d.kind === 'room') {
    const m = editor.mouse_world;
    const min_x = Math.min(d.start.x, m.x), max_x = Math.max(d.start.x, m.x);
    const min_z = Math.min(d.start.z, m.z), max_z = Math.max(d.start.z, m.z);
    const tl = world_to_screen(min_x, min_z), br = world_to_screen(max_x, max_z);
    ctx.strokeStyle = WALL_COLOR_SEL; ctx.lineWidth = 1.5;
    ctx.beginPath(); ctx.rect(tl.x, tl.y, br.x - tl.x, br.y - tl.y); ctx.stroke();
  } else if (d.kind === 'stair') {
    ctx.strokeStyle = '#ffffff'; ctx.lineWidth = 1.5;
    const a = world_to_screen(d.a[0], d.a[1]);
    const b = world_to_screen(editor.mouse_world.x, editor.mouse_world.z);
    ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); ctx.stroke();
  }
  ctx.setLineDash([]);
}

function draw_selection_handles() {
  const sel = editor.selection;
  if (!sel) return;
  if (sel.kind === 'room') {
    const room = rooms[sel.idx];
    if (!room) return;
    for (const [cx, cz] of [
      [room.min_x, room.min_z],
      [room.max_x, room.min_z],
      [room.max_x, room.max_z],
      [room.min_x, room.max_z],
    ]) {
      const sp = world_to_screen(cx, cz);
      ctx.fillStyle = WALL_COLOR_SEL;
      ctx.beginPath(); ctx.arc(sp.x, sp.y, 6, 0, Math.PI * 2); ctx.fill();
      ctx.strokeStyle = '#0c0e13'; ctx.lineWidth = 1; ctx.stroke();
    }
  } else if (sel.kind === 'stair' || sel.kind === 'stair_vertex') {
    const s = level.stairs[sel.idx];
    if (!s) return;
    for (const p of [s.center_a, s.center_b]) {
      const sp = world_to_screen(p[0], p[1]);
      ctx.fillStyle = WALL_COLOR_SEL;
      ctx.beginPath(); ctx.arc(sp.x, sp.y, 5, 0, Math.PI * 2); ctx.fill();
      ctx.strokeStyle = '#0c0e13'; ctx.lineWidth = 1; ctx.stroke();
    }
  } else if (sel.kind === 'light') {
    const L = level.lights[sel.idx];
    if (!L) return;
    const sp = world_to_screen(L.pos[0], L.pos[2]);
    ctx.fillStyle = WALL_COLOR_SEL;
    ctx.beginPath(); ctx.arc(sp.x, sp.y, 7, 0, Math.PI * 2); ctx.fill();
    ctx.strokeStyle = '#0c0e13'; ctx.lineWidth = 1; ctx.stroke();
  }
}

// -------- Hit testing ----------------------------------------------------------------

function dist_point_to_segment(px, pz, ax, az, bx, bz) {
  const dx = bx - ax, dz = bz - az;
  const len_sq = dx * dx + dz * dz;
  if (len_sq < 1e-8) return Math.sqrt((px - ax) ** 2 + (pz - az) ** 2);
  let t = ((px - ax) * dx + (pz - az) * dz) / len_sq;
  t = Math.max(0, Math.min(1, t));
  return Math.sqrt((px - ax - dx * t) ** 2 + (pz - az - dz * t) ** 2);
}

function point_in_polygon(poly, px, pz) {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const ax = poly[i][0], az = poly[i][1], bx = poly[j][0], bz = poly[j][1];
    if ((az > pz) !== (bz > pz)) {
      const x_at = (bx - ax) * (pz - az) / (bz - az) + ax;
      if (px < x_at) inside = !inside;
    }
  }
  return inside;
}

function hit_test(wx, wz) {
  const px_tol = 8 / editor.view.scale;
  let best = null, best_dist = Infinity;

  const check_pt = (vx, vz, kind, idx, sub) => {
    const d = Math.sqrt((wx - vx) ** 2 + (wz - vz) ** 2);
    if (d < px_tol && d < best_dist) { best = { kind, idx, sub }; best_dist = d; }
  };

  // Room corners — highest priority so resize handles always win over body clicks
  for (let i = 0; i < rooms.length; i++) {
    const r = rooms[i];
    for (const [corner, cx, cz] of [
      ['TL', r.min_x, r.min_z], ['TR', r.max_x, r.min_z],
      ['BR', r.max_x, r.max_z], ['BL', r.min_x, r.max_z],
    ]) {
      const d = Math.sqrt((wx - cx) ** 2 + (wz - cz) ** 2);
      if (d < px_tol && d < best_dist) { best = { kind: 'room', idx: i, corner }; best_dist = d; }
    }
  }
  if (best) return best;

  // Stair and light vertex handles
  for (let i = 0; i < level.stairs.length; i++) {
    check_pt(level.stairs[i].center_a[0], level.stairs[i].center_a[1], 'stair_vertex', i, 'a');
    check_pt(level.stairs[i].center_b[0], level.stairs[i].center_b[1], 'stair_vertex', i, 'b');
  }
  for (let i = 0; i < level.lights.length; i++)
    check_pt(level.lights[i].pos[0], level.lights[i].pos[2], 'light_vertex', i, null);
  if (best) return best;

  // Room walls → select room as entity
  for (let i = 0; i < level.walls.length; i++) {
    const w = level.walls[i];
    if (!w.roomId) continue;
    const d = dist_point_to_segment(wx, wz, w.a[0], w.a[2], w.b[0], w.b[2]);
    if (d < px_tol) {
      const ri = rooms.findIndex(r => r.id === w.roomId);
      if (ri >= 0) return { kind: 'room', idx: ri, corner: null };
    }
  }

  // Room sector fill → select room as entity
  for (let i = 0; i < level.sectors.length; i++) {
    const s = level.sectors[i];
    if (!s.roomId) continue;
    if (point_in_polygon(s.polygon, wx, wz)) {
      const ri = rooms.findIndex(r => r.id === s.roomId);
      if (ri >= 0) return { kind: 'room', idx: ri, corner: null };
    }
  }

  return null;
}

// -------- Mouse & keyboard -----------------------------------------------------------

function canvas_event_pos(ev) {
  const rect = canvas.getBoundingClientRect();
  return { x: ev.clientX - rect.left, y: ev.clientY - rect.top };
}

canvas.addEventListener('mousemove', (ev) => {
  const p = canvas_event_pos(ev);
  editor.mouse_screen = p;
  const w = snap_world(screen_to_world(p.x, p.y));
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
      const norm = normalize_hit(hit);
      editor.selection = norm;
      editor.dragging = !!norm;
      if (norm?.kind === 'room') {
        const room = rooms[norm.idx];
        editor.drag_anchor = {
          wx: w.x, wz: w.z,
          room_bounds: { min_x: room.min_x, min_z: room.min_z, max_x: room.max_x, max_z: room.max_z },
        };
      } else {
        editor.drag_anchor = null;
      }
      refresh_selection_panel();
      break;
    }
    case 'drag_room': {
      editor.draft = { kind: 'room', start: { x: w.x, z: w.z } };
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
      level.lights.push({
        pos: [w.x, editor.default_ceiling_y - 0.6, w.z],
        color: [2.4, 2.1, 1.7], intensity: 1.0, radius: 8.0,
      });
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
  if (editor.tool === 'drag_room' && editor.draft?.kind === 'room') {
    const start = editor.draft.start;
    const m = editor.mouse_world;
    const min_x = Math.min(start.x, m.x), max_x = Math.max(start.x, m.x);
    const min_z = Math.min(start.z, m.z), max_z = Math.max(start.z, m.z);
    const idx = create_room(min_x, min_z, max_x, max_z);
    editor.draft = null;
    if (idx >= 0) {
      editor.selection = { kind: 'room', idx, corner: null };
      refresh_selection_panel();
    }
    refresh_counts();
    render();
  }
});

canvas.addEventListener('wheel', (ev) => {
  ev.preventDefault();
  const p = canvas_event_pos(ev);
  const wb = screen_to_world(p.x, p.y);
  editor.view.scale = Math.max(4, Math.min(300, editor.view.scale * (ev.deltaY < 0 ? 1.15 : 1 / 1.15)));
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
  if ((ev.key === 'Backspace' || ev.key === 'Delete') && editor.selection) {
    delete_selection(); refresh_counts(); refresh_selection_panel(); render();
  }
});
window.addEventListener('keyup', (ev) => { if (ev.code === 'Space') editor.space_held = false; });

// -------- Selection ------------------------------------------------------------------

function normalize_hit(hit) {
  if (!hit) return null;
  if (hit.kind === 'stair_vertex')  return { kind: 'stair_vertex',  idx: hit.idx, sub: hit.sub };
  if (hit.kind === 'light_vertex')  return { kind: 'light',          idx: hit.idx };
  return hit;
}

function apply_drag(w) {
  const sel = editor.selection;
  if (!sel) return;

  if (sel.kind === 'room') {
    const room = rooms[sel.idx];
    if (!room) return;
    if (sel.corner) {
      resize_room_corner(room, sel.corner, w.x, w.z);
    } else if (editor.drag_anchor) {
      const dx = w.x - editor.drag_anchor.wx;
      const dz = w.z - editor.drag_anchor.wz;
      const orig = editor.drag_anchor.room_bounds;
      room.min_x = orig.min_x + dx;
      room.max_x = orig.max_x + dx;
      room.min_z = orig.min_z + dz;
      room.max_z = orig.max_z + dz;
      update_room_geometry(room);
    }
  } else if (sel.kind === 'stair_vertex') {
    const s = level.stairs[sel.idx];
    if (sel.sub === 'a') s.center_a = [w.x, w.z];
    else                 s.center_b = [w.x, w.z];
  } else if (sel.kind === 'light') {
    level.lights[sel.idx].pos[0] = w.x;
    level.lights[sel.idx].pos[2] = w.z;
  } else if (sel.kind === 'spawn') {
    level.spawn.pos[0] = w.x;
    level.spawn.pos[2] = w.z;
  }
}

function delete_selection() {
  const sel = editor.selection;
  if (!sel) return;
  if (sel.kind === 'room') {
    delete_room(sel.idx);
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
  if (sel.kind === 'room') {
    info.innerHTML = render_room_edit(sel.idx, rooms[sel.idx]);
    wire_room_edit(sel.idx);
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

function render_room_edit(i, room) {
  if (!room) return '<div>—</div>';
  const w = (room.max_x - room.min_x).toFixed(2);
  const d = (room.max_z - room.min_z).toFixed(2);
  return `
    <div>room ${i} &middot; ${w}&times;${d}m</div>
    ${field('f-room-fy', 'floor_y', room.floor_y)}
    ${field('f-room-cy', 'ceiling_y', room.ceiling_y)}
  `;
}
function wire_room_edit(i) {
  const room = rooms[i];
  if (!room) return;
  document.getElementById('f-room-fy').oninput = (e) => {
    room.floor_y = parseFloat(e.target.value); update_room_geometry(room); render();
  };
  document.getElementById('f-room-cy').oninput = (e) => {
    room.ceiling_y = parseFloat(e.target.value); update_room_geometry(room); render();
  };
}

function render_stair_edit(i, s) {
  return `<div>stair ${i}</div>
    ${field('f-stair-w', 'width', s.width)}
    ${field('f-stair-fy', 'from_y', s.from_y)}
    ${field('f-stair-ty', 'to_y', s.to_y)}
    ${field('f-stair-steps', 'steps', s.steps, '1')}`;
}
function wire_stair_edit(i) {
  const s = level.stairs[i];
  document.getElementById('f-stair-w').oninput     = (e) => { s.width  = parseFloat(e.target.value); render(); };
  document.getElementById('f-stair-fy').oninput    = (e) => { s.from_y = parseFloat(e.target.value); render(); };
  document.getElementById('f-stair-ty').oninput    = (e) => { s.to_y   = parseFloat(e.target.value); render(); };
  document.getElementById('f-stair-steps').oninput = (e) => { s.steps  = parseInt(e.target.value, 10); render(); };
}

function render_light_edit(i, L) {
  return `<div>light ${i}</div>
    ${field('f-light-y', 'pos.y', L.pos[1])}
    ${field('f-light-i', 'intensity', L.intensity, '0.05')}
    ${field('f-light-r', 'color.r', L.color[0], '0.1')}
    ${field('f-light-g', 'color.g', L.color[1], '0.1')}
    ${field('f-light-b', 'color.b', L.color[2], '0.1')}
    ${field('f-light-rad', 'radius', L.radius ?? 8.0, '0.5')}`;
}
function wire_light_edit(i) {
  const L = level.lights[i];
  document.getElementById('f-light-y').oninput   = (e) => { L.pos[1]    = parseFloat(e.target.value); render(); };
  document.getElementById('f-light-i').oninput   = (e) => { L.intensity = parseFloat(e.target.value); render(); };
  document.getElementById('f-light-r').oninput   = (e) => { L.color[0]  = parseFloat(e.target.value); render(); };
  document.getElementById('f-light-g').oninput   = (e) => { L.color[1]  = parseFloat(e.target.value); render(); };
  document.getElementById('f-light-b').oninput   = (e) => { L.color[2]  = parseFloat(e.target.value); render(); };
  document.getElementById('f-light-rad').oninput = (e) => { L.radius    = parseFloat(e.target.value); render(); };
}

function render_spawn_edit() {
  return `<div>spawn</div>
    ${field('f-spawn-y', 'pos.y', level.spawn.pos[1])}
    ${field('f-spawn-yaw', 'yaw_deg', level.spawn.yaw_deg, '5')}`;
}
function wire_spawn_edit() {
  document.getElementById('f-spawn-y').oninput   = (e) => { level.spawn.pos[1]  = parseFloat(e.target.value); render(); };
  document.getElementById('f-spawn-yaw').oninput = (e) => { level.spawn.yaw_deg = parseFloat(e.target.value); render(); };
}

// -------- Counts ---------------------------------------------------------------------

function refresh_counts() {
  document.getElementById('counts').innerHTML =
    `<div>rooms</div><div>${rooms.length}</div>` +
    `<div>sectors</div><div>${level.sectors.length}</div>` +
    `<div>walls</div><div>${level.walls.length}</div>` +
    `<div>stairs</div><div>${level.stairs.length}</div>` +
    `<div>lights</div><div>${level.lights.length}</div>`;
}

// -------- Tool UI --------------------------------------------------------------------

const TOOL_HINTS = {
  select:   'click room to select. drag corner handles to resize. drag interior to move. delete = remove.',
  drag_room:'drag to create a rectangular room (4 walls + sector).',
  stair:    'click bottom center, then top center.',
  light:    'click to place a point light.',
  spawn:    'click to set player spawn position.',
  erase:    'click anything to delete it.',
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

document.getElementById('level-name').oninput        = (e) => { level.name        = e.target.value; };
document.getElementById('level-wall-height').oninput = (e) => { level.wall_height = parseFloat(e.target.value); };
document.getElementById('grid-step').oninput         = (e) => { editor.grid_step  = parseFloat(e.target.value); render(); };
document.getElementById('snap').onchange             = (e) => { editor.snap       = e.target.checked; };
document.getElementById('default-floor-y').oninput   = (e) => { editor.default_floor_y   = parseFloat(e.target.value); };
document.getElementById('default-ceiling-y').oninput = (e) => { editor.default_ceiling_y = parseFloat(e.target.value); };

// -------- Export / import ------------------------------------------------------------

document.getElementById('export-json').onclick = () => {
  const out = {
    version: 3,
    name: level.name,
    wall_height: level.wall_height,
    ambient: level.ambient,
    spawn: level.spawn,
    sectors: level.sectors,
    walls: level.walls,
    stairs: level.stairs,
    lights: level.lights,
    editorRefs: {
      rooms: rooms.map(r => ({
        id: r.id,
        min_x: r.min_x, min_z: r.min_z, max_x: r.max_x, max_z: r.max_z,
        floor_y: r.floor_y, ceiling_y: r.ceiling_y,
      })),
    },
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
  try { import_level(await (await fetch('../assets/maps/spencer_mansion_v3.json')).text()); }
  catch (e) { alert('Failed to load map: ' + e); }
};

function import_level(text) {
  let obj;
  try { obj = JSON.parse(text); } catch (e) { alert('json parse error: ' + e); return; }
  if (obj.version !== 3) { alert('expected version 3 (got ' + obj.version + ')'); return; }

  level.name        = obj.name || '';
  level.wall_height = obj.wall_height ?? 3.2;
  level.ambient     = obj.ambient ?? [0.07, 0.08, 0.11];
  level.spawn       = obj.spawn ?? { pos: [0, 0, 0], yaw_deg: 0 };
  level.sectors = (obj.sectors ?? []).map(s => ({
    id: s.id ?? '',
    polygon: s.polygon ?? [],
    floor_y: s.floor_y ?? 0,
    ceiling_y: s.ceiling_y ?? 3.2,
    roomId: s.roomId,
  }));
  level.walls = (obj.walls ?? []).map(w => ({
    a: w.a ?? [0, 0, 0],
    b: w.b ?? [1, 0, 0],
    height: w.height ?? (obj.wall_height ?? 3.2),
    roomId: w.roomId,
  }));
  level.stairs = (obj.stairs ?? []).map(s => ({
    center_a: s.center_a, center_b: s.center_b,
    width: s.width ?? 2, from_y: s.from_y ?? 0, to_y: s.to_y ?? 3.2, steps: s.steps ?? 8,
  }));
  level.lights = (obj.lights ?? []).map(L => ({
    pos: L.pos, color: L.color ?? [2.4, 2.1, 1.7], intensity: L.intensity ?? 1.0, radius: L.radius ?? 8.0,
  }));

  rooms.length = 0;
  for (const r of obj.editorRefs?.rooms ?? []) {
    rooms.push({
      id: r.id,
      min_x: r.min_x, min_z: r.min_z, max_x: r.max_x, max_z: r.max_z,
      floor_y: r.floor_y ?? 0, ceiling_y: r.ceiling_y ?? 3.2,
    });
  }

  editor.selection = null;
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
  let minx = Infinity, maxx = -Infinity, minz = Infinity, maxz = -Infinity;
  for (const p of pts) {
    minx = Math.min(minx, p[0]); maxx = Math.max(maxx, p[0]);
    minz = Math.min(minz, p[1]); maxz = Math.max(maxz, p[1]);
  }
  const w = canvas.clientWidth, h = canvas.clientHeight, pad = 40;
  editor.view.scale = Math.max(4, Math.min(60, (w - pad * 2) / Math.max(maxx - minx, 1), (h - pad * 2) / Math.max(maxz - minz, 1)));
  editor.view.ox = pad - minx * editor.view.scale;
  editor.view.oy = pad - minz * editor.view.scale;
}

// -------- Init -----------------------------------------------------------------------

refresh_counts();
refresh_selection_panel();
resize_canvas();
