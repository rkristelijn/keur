#!/usr/bin/env node
// keur.js — npx launcher for keur (ADR-045 step 4b, option A: lazy fetch).
//
// On first run this fetches + checksum-verifies the native keur binary (see
// lib/fetch-binary.js), caches it, then execs it — passing through argv and the
// exit code so `npx keur check` behaves exactly like a locally installed keur
// (exit 1 on BLOCK fails the caller). Subsequent runs reuse the cached binary.
'use strict';

const { spawnSync } = require('child_process');
const { ensureBinary } = require('../lib/fetch-binary');

(async () => {
  let binPath;
  try {
    binPath = await ensureBinary();
  } catch (err) {
    console.error(`keur: ${err.message}`);
    process.exit(1);
  }

  const result = spawnSync(binPath, process.argv.slice(2), { stdio: 'inherit' });
  if (result.error) {
    console.error(`keur: failed to run binary: ${result.error.message}`);
    process.exit(1);
  }
  process.exit(result.status === null ? 1 : result.status);
})();
