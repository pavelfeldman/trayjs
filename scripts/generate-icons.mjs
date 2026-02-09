/**
 * Generates platform-specific tray icons from a source SVG.
 *
 * Usage: node scripts/generate-icons.mjs <input.svg> <output-dir>
 *
 * Produces:
 *   icon.png  – white, 44×44 (macOS/Linux)
 *   icon.ico  – color, multi-size ICO (Windows)
 */

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
import { Resvg } from '@resvg/resvg-js';

const [,, svgPath, outDir] = process.argv;
if (!svgPath || !outDir) {
  console.error('Usage: node scripts/generate-icons.mjs <input.svg> <output-dir>');
  process.exit(1);
}

mkdirSync(outDir, { recursive: true });
const svgSource = readFileSync(svgPath, 'utf8');

// ── Crop SVG to a tight square around content ───────────────────────
function cropSvg(svg, padding = 2) {
  const bbox = new Resvg(svg).getBBox();
  if (!bbox)
    return svg;
  const side = Math.max(bbox.width, bbox.height) + padding * 2;
  const cx = bbox.x + bbox.width / 2;
  const cy = bbox.y + bbox.height / 2;
  const vb = `${cx - side / 2} ${cy - side / 2} ${side} ${side}`;
  return svg
    .replace(/viewBox="[^"]*"/, `viewBox="${vb}"`)
    .replace(/width="[^"]*"/, `width="${side}"`)
    .replace(/height="[^"]*"/, `height="${side}"`);
}

// ── Recolor all fills ───────────────────────────────────────────────
function recolor(svg, color) {
  return svg.replace(/fill="#[0-9a-fA-F]{3,8}"/g, `fill="${color}"`);
}

// ── Render SVG → PNG at given pixel width ───────────────────────────
function renderPng(svg, width) {
  const resvg = new Resvg(svg, { fitTo: { mode: 'width', value: width } });
  return Buffer.from(resvg.render().asPng());
}

// ── Build multi-size ICO from PNG buffers ───────────────────────────
function buildIco(pngs, sizes) {
  const count = pngs.length;
  const dirSize = 6 + 16 * count;
  const total = dirSize + pngs.reduce((s, p) => s + p.length, 0);
  const buf = Buffer.alloc(total);
  let off = 0;
  buf.writeUInt16LE(0, off); off += 2;
  buf.writeUInt16LE(1, off); off += 2;
  buf.writeUInt16LE(count, off); off += 2;
  let dataOff = dirSize;
  for (let i = 0; i < count; i++) {
    buf.writeUInt8(sizes[i] < 256 ? sizes[i] : 0, off++);
    buf.writeUInt8(sizes[i] < 256 ? sizes[i] : 0, off++);
    buf.writeUInt8(0, off++);
    buf.writeUInt8(0, off++);
    buf.writeUInt16LE(1, off); off += 2;
    buf.writeUInt16LE(32, off); off += 2;
    buf.writeUInt32LE(pngs[i].length, off); off += 4;
    buf.writeUInt32LE(dataOff, off); off += 4;
    dataOff += pngs[i].length;
  }
  for (const png of pngs) { png.copy(buf, off); off += png.length; }
  return buf;
}

// ── Generate all icons ──────────────────────────────────────────────
const cropped = cropSvg(svgSource);

// macOS + Linux: white on transparent, 32px (16pt @2x Retina)
const png = renderPng(recolor(cropped, '#FFFFFF'), 32);
writeFileSync(join(outDir, 'icon.png'), png);
console.log('  icon.png  32×32  white (macOS/Linux)');

// Windows: color ICO at 16, 32, 48, 256
const icoSizes = [16, 32, 48, 256];
const icoPngs = icoSizes.map(sz => renderPng(cropped, sz));
writeFileSync(join(outDir, 'icon.ico'), buildIco(icoPngs, icoSizes));
console.log('  icon.ico  16+32+48+256 color (Windows)');

console.log('Done.');
