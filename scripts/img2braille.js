#!/usr/bin/env node
/**
 * img2braille.js — convert a PNG to (optionally coloured) braille text.
 *
 * A headless, dependency-free reproduction of rkristelijn/braille-ascii's
 * algorithm (its React canvas version). Instead of a browser canvas it reads a
 * raw RGBA buffer produced by macOS `sips` (or any PPM), so it runs offline in
 * CI / a shell. Used to bake KEUR's DOOM-style logo into the binary as a static
 * braille asset (ADR-002: no runtime image lib in the C++ gate — generate once).
 *
 * Braille cell = 2x4 dots -> one U+28xx glyph. A dot is "on" when the source
 * pixel passes the brightness threshold. Colour (optional) is the average RGB of
 * the cell, emitted as an ANSI 256 truecolour-ish escape (same rgbToAnsi map).
 *
 * Usage:
 *   node img2braille.js <raw.rgba> <width> <height> [--cols N] [--color]
 *                       [--threshold T] [--invert]
 * Produces braille text on stdout.
 */
'use strict';

// Braille dot bit order (matches the Unicode 2x4 layout used by the utility).
const DOTS = [[0,0],[0,1],[0,2],[1,0],[1,1],[1,2],[0,3],[1,3]];

function rgbToAnsi(r, g, b) {
  if (r === g && g === b) {
    if (r < 8) return 16;
    if (r > 248) return 231;
    return Math.round(((r - 8) / 247) * 24) + 232;
  }
  const ar = Math.round((r / 255) * 5);
  const ag = Math.round((g / 255) * 5);
  const ab = Math.round((b / 255) * 5);
  return 16 + 36 * ar + 6 * ag + ab;
}

function main() {
  const [rawPath, wStr, hStr, ...rest] = process.argv.slice(2);
  if (!rawPath) {
    console.error('usage: img2braille.js <raw.rgba> <width> <height> [--cols N] [--color] [--threshold T] [--invert]');
    process.exit(2);
  }
  const srcW = parseInt(wStr, 10);
  const srcH = parseInt(hStr, 10);
  const cols = (() => { const i = rest.indexOf('--cols'); return i >= 0 ? parseInt(rest[i + 1], 10) : 100; })();
  const color = rest.includes('--color');
  const invert = rest.includes('--invert');
  const threshold = (() => { const i = rest.indexOf('--threshold'); return i >= 0 ? parseInt(rest[i + 1], 10) : 128; })();

  const fs = require('fs');
  const buf = fs.readFileSync(rawPath);
  // One braille char = 2 dot-columns x 4 dot-rows. To land on exactly `cols`
  // braille columns we sample the source at a fixed step. `stepX` source pixels
  // map to one dot horizontally; a cell is 2 dots wide, so a cell spans 2*stepX
  // source px. Vertically a terminal char is ~2x taller than wide, so we use the
  // same stepX per dot vertically too (4 dots tall) — that keeps the on-screen
  // aspect close to the source.
  const stepX = Math.max(1, srcW / (cols * 2));   // source px per dot (float)
  const bw = cols;                                 // braille cols == requested
  const bh = Math.floor(srcH / (stepX * 4));       // braille rows (aspect-kept)

  const at = (x, y) => {
    const xi = Math.floor(x), yi = Math.floor(y);
    if (xi < 0 || yi < 0 || xi >= srcW || yi >= srcH) return null;
    const idx = (yi * srcW + xi) * 4;
    return [buf[idx], buf[idx + 1], buf[idx + 2], buf[idx + 3]];
  };

  const lines = [];
  for (let by = 0; by < bh; by++) {
    let line = '';
    for (let bx = 0; bx < bw; bx++) {
      let code = 0x2800;
      let r = 0, g = 0, bl = 0, cnt = 0, onCnt = 0;
      DOTS.forEach((dot, i) => {
        const sx = (bx * 2 + dot[0]) * stepX;
        const sy = (by * 4 + dot[1]) * stepX;
        const p = at(sx, sy);
        if (!p) return;
        const [pr, pg, pb, pa] = p;
        // Treat transparent as background.
        const bright = pa < 32 ? 255 : (pr + pg + pb) / 3;
        const on = invert ? bright > threshold : bright < threshold;
        if (on) {
          code += Math.pow(2, i);
          onCnt++;
          if (color) { r += pr; g += pg; bl += pb; cnt++; }
        }
      });
      const ch = String.fromCharCode(code);
      if (color && cnt > 0 && onCnt > 0) {
        const a = rgbToAnsi(Math.round(r / cnt), Math.round(g / cnt), Math.round(bl / cnt));
        line += `\x1b[38;5;${a}m${ch}`;
      } else {
        line += ch;
      }
    }
    if (color) line += '\x1b[0m';
    lines.push(line);
  }
  process.stdout.write(lines.join('\n') + '\n');
}

main();
