// fps-engine level editor (v2 — sectors + zero-thickness wall segments).
// Single-file vanilla JS; no build step. Writes v2 JSON matching src/json_level.cpp.

const WALL_COLORS = {
  normal: '#78a4d6',
  door: '#d6c878',
  broken: '#d68a78',
  window: '#8ed6c8',
};

const level = {
  version: 2,
  name: 'untitled',
  wall_height: 3.2,
  ambient: [0.07, 0.08, 0.11],
  spawn: { pos: [0, 0, 0], yaw_deg: 0 },
  sectors: [],
  walls: [],
  stairs: [],
  lights: [],
};

const editor = {
  tool: 'select',
  wall_type: 'normal',
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

  const world_left = -editor.view.ox / editor.view.scale;
  const world_top = -editor.view.oy / editor.view.scale;
  const world_right = (w - editor.view.ox) / editor.view.scale;
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

  // Origin axes
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
    // Fill color scaled by floor_y so multi-floor sectors read distinctly.
    const shade = Math.min(255, 48 + Math.round(s.floor_y * 18));
    ctx.fillStyle = `rgba(58, 63, 79, ${Math.min(0.55, 0.25 + s.floor_y * 0.05)})`;
    ctx.fill();
    ctx.strokeStyle = '#555b73';
    ctx.lineWidth = 1;
    ctx.stroke();
    // Label
    const cx = s.polygon.reduce((a, p) => a + p[0], 0) / s.polygon.length;
    const cz = s.polygon.reduce((a, p) => a + p[1], 0) / s.polygon.length;
    const sp = world_to_screen(cx, cz);
    ctx.fillStyle = '#8b92a3';
    ctx.font = '10px ui-monospace, monospace';
    ctx.textAlign = 'center';
    const label = (s.id || `sector ${i}`) + ` · y=${s.floor_y}`;
    ctx.fillText(label, sp.x, sp.y);
  }
}

function draw_walls() {
  for (let i = 0; i < level.walls.length; ++i) {
    const wall = level.walls[i];
    const a = world_to_screen(wall.a[0], wall.a[1]);
    const b = world_to_screen(wall.b[0], wall.b[1]);
    ctx.strokeStyle = WALL_COLORS[wall.type] || '#ffffff';
    ctx.lineWidth = 3;
    ctx.beginPath();
    ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y);
    ctx.stroke();

    if (wall.type === 'door') {
      // Draw a gap in the middle of the segment for visual clarity.
      draw_door_marker(wall);
    }
    if (wall.type === 'broken') draw_dashes(a, b, 6);
    if (wall.type === 'window') draw_dashes(a, b, 3);
  }
}

function draw_door_marker(wall) {
  const dx = wall.b[0] - wall.a[0];
  const dz = wall.b[1] - wall.a[1];
  const len = Math.sqrt(dx * dx + dz * dz);
  if (len < 1e-4) return;
  let off = wall.door_offset;
  let dw = wall.door_width;
  if (dw > len) dw = len;
  if (off < 0) off = 0.5 * (len - dw);
  const t0 = off / len;
  const t1 = (off + dw) / len;
  const p0 = world_to_screen(wall.a[0] + dx * t0, wall.a[1] + dz * t0);
  const p1 = world_to_screen(wall.a[0] + dx * t1, wall.a[1] + dz * t1);
  ctx.strokeStyle = '#0c0e13';
  ctx.lineWidth = 4;
  ctx.beginPath();
  ctx.moveTo(p0.x, p0.y); ctx.lineTo(p1.x, p1.y);
  ctx.stroke();
  ctx.strokeStyle = '#d6c878';
  ctx.lineWidth = 1.5;
  ctx.setLineDash([3, 3]);
  ctx.stroke();
  ctx.setLineDash([]);
}

function draw_dashes(a, b, dash) {
  ctx.strokeStyle = '#0c0e13';
  ctx.lineWidth = 2;
  ctx.setLineDash([dash, dash]);
  ctx.beginPath();
  ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y);
  ctx.stroke();
  ctx.setLineDash([]);
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
    const p0 = world_to_screen(corners[0][0], corners[0][1]);
    ctx.moveTo(p0.x, p0.y);
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

    // Step lines
    ctx.strokeStyle = '#e8b84e';
    ctx.lineWidth = 0.5;
    ctx.beginPath();
    for (let i = 1; i < s.steps; ++i) {
      const t = i / s.steps;
      const mx = s.center_a[0] + dx * t;
      const mz = s.center_a[1] + dz * t;
      const a = world_to_screen(mx + nx * hw, mz + nz * hw);
      const b = world_to_screen(mx - nx * hw, mz - nz * hw);
      ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y);
    }
    ctx.stroke();

    // Arrow pointing from center_a to center_b
    const midx = (s.center_a[0] + s.center_b[0]) / 2;
    const midz = (s.center_a[1] + s.center_b[1]) / 2;
    const startS = world_to_screen(s.center_a[0], s.center_a[1]);
    const endS = world_to_screen(s.center_b[0], s.center_b[1]);
    ctx.strokeStyle = '#e8b84e';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(startS.x, startS.y); ctx.lineTo(endS.x, endS.y);
    ctx.stroke();
    draw_arrowhead(endS.x, endS.y, Math.atan2(endS.y - startS.y, endS.x - startS.x), '#e8b84e');

    // Label
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
    ctx.beginPath();
    ctx.arc(p.x, p.y, 5, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = '#0c0e13';
    ctx.lineWidth = 1;
    ctx.stroke();
    ctx.fillStyle = '#f3e97a';
    ctx.font = '9px ui-monospace, monospace';
    ctx.textAlign = 'left';
    ctx.fillText(`y=${L.pos[1]}`, p.x + 7, p.y + 3);
  }
}

function draw_spawn() {
  const p = world_to_screen(level.spawn.pos[0], level.spawn.pos[2]);
  ctx.fillStyle = '#8cff9c';
  ctx.beginPath();
  ctx.arc(p.x, p.y, 6, 0, Math.PI * 2);
  ctx.fill();
  ctx.strokeStyle = '#0c0e13';
  ctx.lineWidth = 1.5;
  ctx.stroke();
  // Yaw arrow: yaw=0 points along +Z (south in screen coords)
  const yaw = (level.spawn.yaw_deg || 0) * Math.PI / 180;
  const end_wx = level.spawn.pos[0] + Math.sin(yaw) * 1.2;
  const end_wz = level.spawn.pos[2] + Math.cos(yaw) * 1.2;
  const e = world_to_screen(end_wx, end_wz);
  ctx.strokeStyle = '#8cff9c';
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(p.x, p.y); ctx.lineTo(e.x, e.y);
  ctx.stroke();
  draw_arrowhead(e.x, e.y, Math.atan2(e.y - p.y, e.x - p.x), '#8cff9c');
}

function draw_draft() {
  if (!editor.draft) return;
  const d = editor.draft;
  const m = editor.mouse_world;
  ctx.strokeStyle = '#ffffff';
  ctx.lineWidth = 1.5;
  ctx.setLineDash([4, 4]);
  if (d.kind === 'wall') {
    const a = world_to_screen(d.a[0], d.a[1]);
    const bs = world_to_screen(m.x, m.z);
    ctx.beginPath();
    ctx.moveTo(a.x, a.y); ctx.lineTo(bs.x, bs.y);
    ctx.stroke();
  } else if (d.kind === 'sector') {
    ctx.beginPath();
    const p0 = world_to_screen(d.points[0][0], d.points[0][1]);
    ctx.moveTo(p0.x, p0.y);
    for (let k = 1; k < d.points.length; ++k) {
      const p = world_to_screen(d.points[k][0], d.points[k][1]);
      ctx.lineTo(p.x, p.y);
    }
    const ms = world_to_screen(m.x, m.z);
    ctx.lineTo(ms.x, ms.y);
    ctx.stroke();
    // Mark each placed vertex
    ctx.setLineDash([]);
    for (const pt of d.points) {
      const p = world_to_screen(pt[0], pt[1]);
      ctx.fillStyle = '#ffffff';
      ctx.beginPath(); ctx.arc(p.x, p.y, 3, 0, Math.PI * 2); ctx.fill();
    }
  } else if (d.kind === 'stair') {
    const a = world_to_screen(d.a[0], d.a[1]);
    const bs = world_to_screen(m.x, m.z);
    ctx.beginPath();
    ctx.moveTo(a.x, a.y); ctx.lineTo(bs.x, bs.y);
    ctx.stroke();
  }
  ctx.setLineDash([]);
}

function draw_selection_handles() {
  const sel = editor.selection;
  if (!sel) return;
  if (sel.kind === 'wall') {
    const w = level.walls[sel.idx];
    if (!w) return;
    for (const p of [w.a, w.b]) {
      const s = world_to_screen(p[0], p[1]);
      ctx.fillStyle = '#8bc4ff';
      ctx.beginPath(); ctx.arc(s.x, s.y, 5, 0, Math.PI * 2); ctx.fill();
      ctx.strokeStyle = '#0c0e13'; ctx.lineWidth = 1; ctx.stroke();
    }
  } else if (sel.kind === 'sector') {
    const s = level.sectors[sel.idx];
    if (!s) return;
    for (const p of s.polygon) {
      const sp = world_to_screen(p[0], p[1]);
      ctx.fillStyle = '#8bc4ff';
      ctx.beginPath(); ctx.arc(sp.x, sp.y, 5, 0, Math.PI * 2); ctx.fill();
      ctx.strokeStyle = '#0c0e13'; ctx.lineWidth = 1; ctx.stroke();
    }
  } else if (sel.kind === 'stair') {
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
  if (len_sq < 1e-8) {
    const ex = px - ax, ez = pz - az;
    return Math.sqrt(ex * ex + ez * ez);
  }
  let t = ((px - ax) * dx + (pz - az) * dz) / len_sq;
  if (t < 0) t = 0; else if (t > 1) t = 1;
  const cx = ax + dx * t, cz = az + dz * t;
  const ex = px - cx, ez = pz - cz;
  return Math.sqrt(ex * ex + ez * ez);
}

function point_in_polygon(poly, px, pz) {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const ax = poly[i][0], az = poly[i][1];
    const bx = poly[j][0], bz = poly[j][1];
    const crosses = (az > pz) !== (bz > pz);
    if (!crosses) continue;
    const x_at = (bx - ax) * (pz - az) / (bz - az) + ax;
    if (px < x_at) inside = !inside;
  }
  return inside;
}

function hit_test(wx, wz) {
  const px_tol = 8 / editor.view.scale; // ~8px in world units
  let best = null;
  let best_dist = Infinity;

  // Vertex handles first (higher priority than segments)
  const check_vertex = (vx, vz, kind, idx, sub) => {
    const dx = wx - vx, dz = wz - vz;
    const d = Math.sqrt(dx * dx + dz * dz);
    if (d < px_tol && d < best_dist) {
      best = { kind, idx, sub };
      best_dist = d;
    }
  };
  for (let i = 0; i < level.walls.length; ++i) {
    const w = level.walls[i];
    check_vertex(w.a[0], w.a[1], 'wall_vertex', i, 'a');
    check_vertex(w.b[0], w.b[1], 'wall_vertex', i, 'b');
  }
  for (let i = 0; i < level.sectors.length; ++i) {
    const s = level.sectors[i];
    for (let k = 0; k < s.polygon.length; ++k) {
      check_vertex(s.polygon[k][0], s.polygon[k][1], 'sector_vertex', i, k);
    }
  }
  for (let i = 0; i < level.stairs.length; ++i) {
    const s = level.stairs[i];
    check_vertex(s.center_a[0], s.center_a[1], 'stair_vertex', i, 'a');
    check_vertex(s.center_b[0], s.center_b[1], 'stair_vertex', i, 'b');
  }
  for (let i = 0; i < level.lights.length; ++i) {
    const L = level.lights[i];
    check_vertex(L.pos[0], L.pos[2], 'light_vertex', i, null);
  }

  if (best) return best;

  // Segment hits
  for (let i = 0; i < level.walls.length; ++i) {
    const w = level.walls[i];
    const d = dist_point_to_segment(wx, wz, w.a[0], w.a[1], w.b[0], w.b[1]);
    if (d < px_tol && d < best_dist) {
      best = { kind: 'wall', idx: i };
      best_dist = d;
    }
  }

  if (best) return best;

  // Sector hits (fill)
  for (let i = 0; i < level.sectors.length; ++i) {
    if (point_in_polygon(level.sectors[i].polygon, wx, wz)) {
      return { kind: 'sector', idx: i };
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
  let w = screen_to_world(p.x, p.y);
  w = snap_world(w);
  editor.mouse_world = { x: w.x, z: w.z };

  if (editor.panning && editor.pan_start) {
    const dx = p.x - editor.pan_start.sx;
    const dy = p.y - editor.pan_start.sy;
    editor.view.ox = editor.pan_start.ox + dx;
    editor.view.oy = editor.pan_start.oy + dy;
  }
  if (editor.tool === 'select' && editor.dragging && editor.selection) {
    apply_drag(w);
  }
  render();
});

canvas.addEventListener('mousedown', (ev) => {
  const p = canvas_event_pos(ev);
  // Middle mouse or space+drag = pan
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
      editor.dragging = editor.selection ? true : false;
      refresh_selection_panel();
      break;
    }
    case 'wall': {
      if (!editor.draft) {
        editor.draft = { kind: 'wall', a: [w.x, w.z] };
      } else {
        level.walls.push({
          type: editor.wall_type,
          a: editor.draft.a,
          b: [w.x, w.z],
          y0: editor.default_floor_y,
          y1: editor.default_ceiling_y,
          door_width: 1.2,
          door_offset: -1,
          door_height: 2.2,
        });
        editor.draft = null;
      }
      break;
    }
    case 'sector': {
      if (!editor.draft) {
        editor.draft = { kind: 'sector', points: [[w.x, w.z]] };
      } else {
        editor.draft.points.push([w.x, w.z]);
      }
      break;
    }
    case 'stair': {
      if (!editor.draft) {
        editor.draft = { kind: 'stair', a: [w.x, w.z] };
      } else {
        const stair = {
          center_a: editor.draft.a,
          center_b: [w.x, w.z],
          width: 2.0,
          from_y: editor.default_floor_y,
          to_y: editor.default_floor_y + editor.default_ceiling_y,
          steps: 8,
        };
        level.stairs.push(stair);
        editor.draft = null;
        editor.selection = { kind: 'stair', idx: level.stairs.length - 1 };
        refresh_selection_panel();
      }
      break;
    }
    case 'light': {
      level.lights.push({
        pos: [w.x, editor.default_ceiling_y - 0.6, w.z],
        color: [2.4, 2.1, 1.7],
        intensity: 1.0,
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

canvas.addEventListener('mouseup', (ev) => {
  editor.panning = false;
  editor.dragging = false;
});

canvas.addEventListener('wheel', (ev) => {
  ev.preventDefault();
  const p = canvas_event_pos(ev);
  const world_before = screen_to_world(p.x, p.y);
  const factor = ev.deltaY < 0 ? 1.15 : 1 / 1.15;
  editor.view.scale *= factor;
  editor.view.scale = Math.max(4, Math.min(300, editor.view.scale));
  const world_after = screen_to_world(p.x, p.y);
  editor.view.ox += (world_after.x - world_before.x) * editor.view.scale;
  editor.view.oy += (world_after.z - world_before.z) * editor.view.scale;
  render();
}, { passive: false });

window.addEventListener('keydown', (ev) => {
  if (ev.target.tagName === 'INPUT') return;
  if (ev.code === 'Space') { editor.space_held = true; ev.preventDefault(); return; }
  if (ev.key === 'Escape') {
    editor.draft = null;
    editor.selection = null;
    refresh_selection_panel();
    render();
    return;
  }
  if (ev.key === 'Enter' && editor.draft && editor.draft.kind === 'sector') {
    if (editor.draft.points.length >= 3) {
      level.sectors.push({
        id: `sector_${level.sectors.length}`,
        polygon: editor.draft.points,
        floor_y: editor.default_floor_y,
        ceiling_y: editor.default_ceiling_y,
      });
      editor.draft = null;
      refresh_counts();
      render();
    }
    return;
  }
  if (ev.key === 'Backspace' || ev.key === 'Delete') {
    if (editor.selection) {
      delete_selection();
      refresh_counts();
      refresh_selection_panel();
      render();
    }
  }
});

window.addEventListener('keyup', (ev) => {
  if (ev.code === 'Space') editor.space_held = false;
});

// -------- Selection ------------------------------------------------------------------

function normalize_hit(hit) {
  if (!hit) return null;
  if (hit.kind === 'wall_vertex') return { kind: 'wall_vertex', idx: hit.idx, sub: hit.sub };
  if (hit.kind === 'sector_vertex') return { kind: 'sector_vertex', idx: hit.idx, sub: hit.sub };
  if (hit.kind === 'stair_vertex') return { kind: 'stair_vertex', idx: hit.idx, sub: hit.sub };
  if (hit.kind === 'light_vertex') return { kind: 'light', idx: hit.idx };
  return hit;
}

function apply_drag(w) {
  const sel = editor.selection;
  if (!sel) return;
  const snapped = snap_world(w);
  if (sel.kind === 'wall_vertex') {
    const wall = level.walls[sel.idx];
    if (sel.sub === 'a') wall.a = [snapped.x, snapped.z];
    else wall.b = [snapped.x, snapped.z];
  } else if (sel.kind === 'sector_vertex') {
    level.sectors[sel.idx].polygon[sel.sub] = [snapped.x, snapped.z];
  } else if (sel.kind === 'stair_vertex') {
    const s = level.stairs[sel.idx];
    if (sel.sub === 'a') s.center_a = [snapped.x, snapped.z];
    else s.center_b = [snapped.x, snapped.z];
  } else if (sel.kind === 'light') {
    level.lights[sel.idx].pos[0] = snapped.x;
    level.lights[sel.idx].pos[2] = snapped.z;
  } else if (sel.kind === 'spawn') {
    level.spawn.pos[0] = snapped.x;
    level.spawn.pos[2] = snapped.z;
  } else if (sel.kind === 'wall') {
    // drag entire wall — move both endpoints by delta
    // We'd need the previous mouse world; for simplicity, skip whole-wall move and require vertex drag.
  }
}

function delete_selection() {
  const sel = editor.selection;
  if (!sel) return;
  if (sel.kind === 'wall' || sel.kind === 'wall_vertex') {
    level.walls.splice(sel.idx, 1);
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
    const w = level.walls[sel.idx];
    info.innerHTML = render_wall_edit(sel.idx, w);
    wire_wall_edit(sel.idx);
  } else if (sel.kind === 'sector' || sel.kind === 'sector_vertex') {
    const s = level.sectors[sel.idx];
    info.innerHTML = render_sector_edit(sel.idx, s);
    wire_sector_edit(sel.idx);
  } else if (sel.kind === 'stair' || sel.kind === 'stair_vertex') {
    const s = level.stairs[sel.idx];
    info.innerHTML = render_stair_edit(sel.idx, s);
    wire_stair_edit(sel.idx);
  } else if (sel.kind === 'light') {
    const L = level.lights[sel.idx];
    info.innerHTML = render_light_edit(sel.idx, L);
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
  return `
    <div>wall ${i} (${w.type})</div>
    <label>type <select id="f-wall-type">
      <option value="normal"${w.type === 'normal' ? ' selected' : ''}>normal</option>
      <option value="door"${w.type === 'door' ? ' selected' : ''}>door</option>
      <option value="broken"${w.type === 'broken' ? ' selected' : ''}>broken</option>
      <option value="window"${w.type === 'window' ? ' selected' : ''}>window</option>
    </select></label>
    ${field('f-wall-y0', 'y0', w.y0)}
    ${field('f-wall-y1', 'y1', w.y1)}
    ${field('f-wall-dw', 'door_width', w.door_width)}
    ${field('f-wall-doff', 'door_offset (-1=center)', w.door_offset)}
    ${field('f-wall-dh', 'door_height', w.door_height)}
  `;
}

function wire_wall_edit(i) {
  const w = level.walls[i];
  document.getElementById('f-wall-type').onchange = (e) => { w.type = e.target.value; render(); };
  document.getElementById('f-wall-y0').oninput = (e) => { w.y0 = parseFloat(e.target.value); render(); };
  document.getElementById('f-wall-y1').oninput = (e) => { w.y1 = parseFloat(e.target.value); render(); };
  document.getElementById('f-wall-dw').oninput = (e) => { w.door_width = parseFloat(e.target.value); render(); };
  document.getElementById('f-wall-doff').oninput = (e) => { w.door_offset = parseFloat(e.target.value); render(); };
  document.getElementById('f-wall-dh').oninput = (e) => { w.door_height = parseFloat(e.target.value); render(); };
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
  document.getElementById('f-sec-fy').oninput = (e) => { s.floor_y = parseFloat(e.target.value); render(); };
  document.getElementById('f-sec-cy').oninput = (e) => { s.ceiling_y = parseFloat(e.target.value); render(); };
}

function render_stair_edit(i, s) {
  return `
    <div>stair ${i}</div>
    ${field('f-stair-w', 'width', s.width)}
    ${field('f-stair-fy', 'from_y', s.from_y)}
    ${field('f-stair-ty', 'to_y', s.to_y)}
    ${field('f-stair-steps', 'steps', s.steps, '1')}
  `;
}

function wire_stair_edit(i) {
  const s = level.stairs[i];
  document.getElementById('f-stair-w').oninput = (e) => { s.width = parseFloat(e.target.value); render(); };
  document.getElementById('f-stair-fy').oninput = (e) => { s.from_y = parseFloat(e.target.value); render(); };
  document.getElementById('f-stair-ty').oninput = (e) => { s.to_y = parseFloat(e.target.value); render(); };
  document.getElementById('f-stair-steps').oninput = (e) => { s.steps = parseInt(e.target.value, 10); render(); };
}

function render_light_edit(i, L) {
  return `
    <div>light ${i}</div>
    ${field('f-light-y', 'pos.y', L.pos[1])}
    ${field('f-light-i', 'intensity', L.intensity, '0.05')}
    ${field('f-light-r', 'color.r', L.color[0])}
    ${field('f-light-g', 'color.g', L.color[1])}
    ${field('f-light-b', 'color.b', L.color[2])}
  `;
}

function wire_light_edit(i) {
  const L = level.lights[i];
  document.getElementById('f-light-y').oninput = (e) => { L.pos[1] = parseFloat(e.target.value); render(); };
  document.getElementById('f-light-i').oninput = (e) => { L.intensity = parseFloat(e.target.value); render(); };
  document.getElementById('f-light-r').oninput = (e) => { L.color[0] = parseFloat(e.target.value); render(); };
  document.getElementById('f-light-g').oninput = (e) => { L.color[1] = parseFloat(e.target.value); render(); };
  document.getElementById('f-light-b').oninput = (e) => { L.color[2] = parseFloat(e.target.value); render(); };
}

function render_spawn_edit() {
  const s = level.spawn;
  return `
    <div>spawn</div>
    ${field('f-spawn-y', 'pos.y', s.pos[1])}
    ${field('f-spawn-yaw', 'yaw_deg', s.yaw_deg, '5')}
  `;
}

function wire_spawn_edit() {
  const s = level.spawn;
  document.getElementById('f-spawn-y').oninput = (e) => { s.pos[1] = parseFloat(e.target.value); render(); };
  document.getElementById('f-spawn-yaw').oninput = (e) => { s.yaw_deg = parseFloat(e.target.value); render(); };
}

// -------- Counts ---------------------------------------------------------------------

function refresh_counts() {
  const el = document.getElementById('counts');
  el.innerHTML =
    `<div>sectors</div><div>${level.sectors.length}</div>` +
    `<div>walls</div><div>${level.walls.length}</div>` +
    `<div>stairs</div><div>${level.stairs.length}</div>` +
    `<div>lights</div><div>${level.lights.length}</div>`;
}

// -------- Tool UI --------------------------------------------------------------------

const TOOL_HINTS = {
  select: 'click to select vertex / segment / sector. drag handles to move. delete/backspace to remove.',
  wall: 'click two points to place a segment. esc to cancel.',
  sector: 'click points to outline a polygon. enter to close it. esc to cancel.',
  stair: 'click bottom center, then top center. width and heights are editable after placing.',
  light: 'click to place a light. y defaults to (ceiling_y - 0.6).',
  spawn: 'click to set spawn position.',
  erase: 'click something to delete it.',
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

document.querySelectorAll('.chip-palette button').forEach((btn) => {
  btn.onclick = () => {
    editor.wall_type = btn.dataset.type;
    document.querySelectorAll('.chip-palette button').forEach((b) => b.classList.toggle('active', b === btn));
  };
});
document.querySelector('.chip-palette button[data-type="normal"]').classList.add('active');

// -------- Meta fields ----------------------------------------------------------------

document.getElementById('level-name').oninput = (e) => { level.name = e.target.value; };
document.getElementById('level-wall-height').oninput = (e) => { level.wall_height = parseFloat(e.target.value); };
document.getElementById('grid-step').oninput = (e) => { editor.grid_step = parseFloat(e.target.value); render(); };
document.getElementById('snap').onchange = (e) => { editor.snap = e.target.checked; };
document.getElementById('default-floor-y').oninput = (e) => { editor.default_floor_y = parseFloat(e.target.value); };
document.getElementById('default-ceiling-y').oninput = (e) => { editor.default_ceiling_y = parseFloat(e.target.value); };

// -------- Export / import ------------------------------------------------------------

document.getElementById('export-json').onclick = () => {
  const out = {
    version: 2,
    name: level.name,
    wall_height: level.wall_height,
    ambient: level.ambient,
    spawn: level.spawn,
    sectors: level.sectors,
    walls: level.walls,
    stairs: level.stairs,
    lights: level.lights,
  };
  const json = JSON.stringify(out, null, 2);
  const blob = new Blob([json], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = (level.name || 'untitled') + '.json';
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
};

document.getElementById('import-json').onclick = () => document.getElementById('import-file').click();
document.getElementById('import-file').onchange = async (ev) => {
  const file = ev.target.files[0];
  if (!file) return;
  const text = await file.text();
  import_level(text);
};

document.getElementById('load-mansion').onclick = async () => {
  try {
    const resp = await fetch('../maps/mansion.json');
    const text = await resp.text();
    import_level(text);
  } catch (e) {
    alert('Failed to load mansion.json: ' + e);
  }
};

function import_level(text) {
  let obj;
  try { obj = JSON.parse(text); } catch (e) { alert('json parse error: ' + e); return; }
  if (obj.version !== 2) { alert('expected version: 2 (got ' + obj.version + ')'); return; }
  level.name = obj.name || '';
  level.wall_height = obj.wall_height ?? 3.2;
  level.ambient = obj.ambient ?? [0.07, 0.08, 0.11];
  level.spawn = obj.spawn ?? { pos: [0, 0, 0], yaw_deg: 0 };
  level.sectors = obj.sectors ?? [];
  level.walls = (obj.walls ?? []).map((w) => ({
    type: w.type ?? 'normal',
    a: w.a, b: w.b,
    y0: w.y0 ?? 0, y1: w.y1 ?? (obj.wall_height ?? 3.2),
    door_width: w.door_width ?? 1.2,
    door_offset: w.door_offset ?? -1,
    door_height: w.door_height ?? 2.2,
  }));
  level.stairs = (obj.stairs ?? []).map((s) => ({
    center_a: s.center_a, center_b: s.center_b,
    width: s.width ?? 2, from_y: s.from_y ?? 0, to_y: s.to_y ?? 3.2, steps: s.steps ?? 8,
  }));
  level.lights = (obj.lights ?? []).map((L) => ({
    pos: L.pos, color: L.color ?? [2.4, 2.1, 1.7], intensity: L.intensity ?? 1.0,
  }));
  document.getElementById('level-name').value = level.name;
  document.getElementById('level-wall-height').value = level.wall_height;
  frame_to_content();
  refresh_counts();
  refresh_selection_panel();
  render();
}

function frame_to_content() {
  const pts = [];
  for (const s of level.sectors) for (const p of s.polygon) pts.push(p);
  for (const w of level.walls) { pts.push(w.a); pts.push(w.b); }
  for (const s of level.stairs) { pts.push(s.center_a); pts.push(s.center_b); }
  if (pts.length === 0) return;
  let minx = Infinity, maxx = -Infinity, minz = Infinity, maxz = -Infinity;
  for (const p of pts) {
    minx = Math.min(minx, p[0]); maxx = Math.max(maxx, p[0]);
    minz = Math.min(minz, p[1]); maxz = Math.max(maxz, p[1]);
  }
  const w = canvas.clientWidth, h = canvas.clientHeight;
  const pad = 40;
  const scale_x = (w - pad * 2) / Math.max(maxx - minx, 1);
  const scale_z = (h - pad * 2) / Math.max(maxz - minz, 1);
  editor.view.scale = Math.max(4, Math.min(scale_x, scale_z, 60));
  editor.view.ox = pad - minx * editor.view.scale;
  editor.view.oy = pad - minz * editor.view.scale;
}

// -------- Init -----------------------------------------------------------------------

refresh_counts();
refresh_selection_panel();
resize_canvas();
