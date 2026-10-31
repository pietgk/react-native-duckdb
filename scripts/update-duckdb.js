#!/usr/bin/env node
// Moves the package to a DuckDB release and updates the out-of-tree extension
// pins to the commits that release builds against:
//
//   node scripts/update-duckdb.js v2.0.1
//
// bun run test then fails if the pins or patch flags still differ.
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const DUCKDB_REPO = 'https://github.com/duckdb/duckdb.git';
const PACKAGE_DIR = path.resolve(__dirname, '../package');
const DUCKDB_JSON = path.join(PACKAGE_DIR, 'duckdb.json');
const CONFIGURE_SCRIPT = path.join(PACKAGE_DIR, 'scripts/configure-extensions.js');
const EXTENSION_CONFIG = path.join(PACKAGE_DIR, 'duckdb/.github/config/extensions');
const OUT_OF_TREE = ['sqlite_scanner', 'httpfs', 'fts', 'vss'];

function resolveCommit(tag) {
  const output = execFileSync(
    'git',
    ['ls-remote', DUCKDB_REPO, `refs/tags/${tag}`, `refs/tags/${tag}^{}`],
    { encoding: 'utf8' }
  );
  const lines = output.trim().split('\n').filter(Boolean);
  if (lines.length === 0) {
    throw new Error(`DuckDB has no tag ${tag}`);
  }
  // An annotated tag lists the commit it points to last, as tag^{}.
  return lines[lines.length - 1].split('\t')[0];
}

function upstreamPin(name) {
  const config = fs.readFileSync(path.join(EXTENSION_CONFIG, `${name}.cmake`), 'utf8');
  const match = config.match(/GIT_TAG\s+([0-9a-f]{40})/);
  if (!match) {
    throw new Error(`No pinned commit for ${name}`);
  }
  return match[1];
}

function updatePins() {
  let script = fs.readFileSync(CONFIGURE_SCRIPT, 'utf8');
  for (const name of OUT_OF_TREE) {
    const block = new RegExp(`(${name}: \\{[^}]*git_tag: ')[0-9a-f]{40}`);
    if (!block.test(script)) {
      throw new Error(`No pin for ${name} in configure-extensions.js`);
    }
    script = script.replace(block, `$1${upstreamPin(name)}`);
  }
  fs.writeFileSync(CONFIGURE_SCRIPT, script);
}

function main() {
  const tag = process.argv[2];
  if (!tag) {
    console.error('Usage: update-duckdb.js <tag>, for example v2.0.1');
    process.exit(1);
  }

  const commit = resolveCommit(tag);
  fs.writeFileSync(DUCKDB_JSON, `${JSON.stringify({ version: tag, commit }, null, 2)}\n`);
  execFileSync('bash', [path.join(PACKAGE_DIR, 'scripts/fetch-duckdb.sh')], { stdio: 'inherit' });
  updatePins();
  console.log(`DuckDB ${tag} (${commit}). Run bun run test, then build and run the suite.`);
}

main();
