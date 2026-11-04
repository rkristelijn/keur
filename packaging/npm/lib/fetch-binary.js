#!/usr/bin/env node
// fetch-binary.js — lazily fetch + checksum-verify the native keur binary on
// first run (ADR-045 step 4b, option A: no postinstall).
//
// Why no postinstall: lifecycle scripts are unreliable across package managers
// (pnpm blocks dependency build scripts by default since v10; npm/CI often run
// with --ignore-scripts; it is also a supply-chain risk teams disable). So the
// launcher fetches on first use instead — works under npm/pnpm/bun/yarn alike,
// no lifecycle script required.
//
// keur is a C++ binary, not a Node package; this module downloads the matching
// keur-<os>-<arch> asset for THIS package's version and verifies its sha256
// against the release's SHA256SUMS BEFORE trusting it (the same verify-before-
// trust install.sh does, SCA-030). The binary is cached so the fetch happens
// once, not every run.
//
// Pure Node, no dependencies.
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const crypto = require('crypto');
const https = require('https');

const REPO = 'rkristelijn/keur';
const { version } = require('../package.json');
const TAG = `v${version}`;

// Map Node's platform/arch to the release asset install.sh publishes. keur
// ships macos-arm64, linux-amd64, windows-amd64.exe today; Intel macOS uses the
// arm64 build under Rosetta, exactly like install.sh.
function resolveAsset() {
  const platform = os.platform();
  const arch = os.arch();
  if (platform === 'darwin') return 'keur-macos-arm64';
  if (platform === 'linux') {
    if (arch === 'x64') return 'keur-linux-amd64';
    throw new Error(`no linux-${arch} release asset yet (only linux-amd64)`);
  }
  if (platform === 'win32') {
    if (arch === 'x64') return 'keur-windows-amd64.exe';
    throw new Error(`no windows-${arch} release asset yet (only windows-amd64)`);
  }
  throw new Error(`unsupported platform: ${platform}/${arch}`);
}

// Cache dir: honour XDG, fall back to ~/.cache. Versioned so a package upgrade
// fetches the new binary instead of reusing a stale one.
function cacheDir() {
  const base =
    process.env.KEUR_CACHE_DIR ||
    process.env.XDG_CACHE_HOME ||
    path.join(os.homedir(), '.cache');
  return path.join(base, 'keur', TAG);
}

function download(url) {
  return new Promise((resolve, reject) => {
    https
      .get(url, { headers: { 'User-Agent': 'keur-npm' } }, (res) => {
        if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
          res.resume();
          resolve(download(res.headers.location)); // follow GitHub CDN redirect
          return;
        }
        if (res.statusCode !== 200) {
          res.resume();
          reject(new Error(`HTTP ${res.statusCode} for ${url}`));
          return;
        }
        const chunks = [];
        res.on('data', (c) => chunks.push(c));
        res.on('end', () => resolve(Buffer.concat(chunks)));
      })
      .on('error', reject);
  });
}

const sha256 = (buf) => crypto.createHash('sha256').update(buf).digest('hex');

// Return the path to a ready-to-run keur binary, fetching + verifying it once.
async function ensureBinary() {
  const asset = resolveAsset();
  const dir = cacheDir();
  const isWin = os.platform() === 'win32';
  const out = path.join(dir, isWin ? 'keur.exe' : 'keur');

  if (fs.existsSync(out)) return out; // cached from a previous run

  const base = `https://github.com/${REPO}/releases/download/${TAG}`;
  let binary;
  let sumsRaw;
  try {
    [binary, sumsRaw] = await Promise.all([
      download(`${base}/${asset}`),
      download(`${base}/SHA256SUMS`),
    ]);
  } catch (err) {
    throw new Error(
      `could not download keur ${TAG} (${asset}): ${err.message}. ` +
        `Check your network, or install from https://github.com/${REPO}/releases.`,
    );
  }

  // Verify BEFORE trusting: find "<sha>  <asset>" and compare.
  const line = sumsRaw
    .toString('utf8')
    .split('\n')
    .find((l) => l.trim().endsWith(` ${asset}`));
  if (!line) throw new Error(`no checksum for ${asset} in SHA256SUMS`);
  const expected = line.trim().split(/\s+/)[0];
  const actual = sha256(binary);
  if (actual !== expected) {
    throw new Error(
      `checksum MISMATCH for ${asset} — refusing to run (expected ${expected}, got ${actual})`,
    );
  }

  // Write atomically (temp + rename) so a concurrent/interrupted run never
  // leaves a half-written binary that a later run would trust.
  fs.mkdirSync(dir, { recursive: true });
  const tmp = `${out}.${process.pid}.tmp`;
  fs.writeFileSync(tmp, binary, { mode: 0o755 });
  fs.renameSync(tmp, out);
  return out;
}

module.exports = { ensureBinary, resolveAsset, cacheDir, TAG };
