#!/usr/bin/env node

import { existsSync, readFileSync, writeFileSync, readdirSync } from 'fs';
import { dirname, join } from 'path';
import { execSync } from 'child_process';
import { fileURLToPath } from 'url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');

function readJSON(p) { return JSON.parse(readFileSync(p, 'utf8')); }
function writeJSON(p, obj) { writeFileSync(p, JSON.stringify(obj, null, 2) + '\n'); }

// Read current version and bump patch.
const rootPkg = readJSON(join(root, 'package.json'));
const parts = rootPkg.version.split('.').map(Number);
parts[2]++;
const next = parts.join('.');
console.log(`${rootPkg.version} -> ${next}`);

// Update root package.json.
rootPkg.version = next;
for (const key of Object.keys(rootPkg.optionalDependencies || {}))
  rootPkg.optionalDependencies[key] = next;
writeJSON(join(root, 'package.json'), rootPkg);

// Update each binary package.json.
for (const dir of readdirSync(join(root, 'binaries'))) {
  const p = join(root, 'binaries', dir, 'package.json');
  if (!existsSync(p)) continue;
  const pkg = readJSON(p);
  pkg.version = next;
  writeJSON(p, pkg);
}

// Regenerate lockfile.
execSync('npm i', { cwd: root, stdio: 'inherit' });
