const { spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const { name: PACKAGE_NAME } = require('../package.json');

const SCRIPT = path.resolve(__dirname, '../scripts/configure-extensions.js');
const DUCKDB_CONFIG = path.resolve(__dirname, '../duckdb/.github/config/extensions');

let workDir;
let duckdbDir;

function run(args, cwd = workDir) {
  return spawnSync('node', [SCRIPT, '--duckdb-path', duckdbDir, ...args], {
    cwd,
    encoding: 'utf8',
  });
}

function generatedConfig() {
  return fs.readFileSync(path.join(duckdbDir, 'extension/extension_config_local.cmake'), 'utf8');
}

function writeApp(dir, config) {
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(
    path.join(dir, 'package.json'),
    JSON.stringify({
      dependencies: { [PACKAGE_NAME]: '*' },
      [PACKAGE_NAME]: { build: { extensions: config } },
    })
  );
}

beforeEach(() => {
  workDir = fs.mkdtempSync(path.join(os.tmpdir(), 'rndb-ext-'));
  duckdbDir = path.join(workDir, 'duckdb');
  fs.mkdirSync(path.join(duckdbDir, 'extension'), { recursive: true });
  fs.writeFileSync(path.join(duckdbDir, 'CMakeLists.txt'), '');
});

afterEach(() => {
  fs.rmSync(workDir, { recursive: true, force: true });
});

describe('configure-extensions', () => {
  it('uses the --extensions list and trims entries', () => {
    const result = run(['--extensions', ' json , icu ']);
    expect(result.status).toBe(0);
    const config = generatedConfig();
    expect(config).toContain('duckdb_extension_load(json)');
    expect(config).toContain('duckdb_extension_load(icu)');
  });

  it('links every extension statically', () => {
    run(['--extensions', 'core_functions,json,httpfs']);
    expect(generatedConfig()).toContain(
      'duckdb_extension_statically_link(core_functions json httpfs)'
    );
  });

  it('skips default extensions that were not requested', () => {
    run(['--extensions', 'core_functions,json']);
    expect(generatedConfig()).toContain('set(SKIP_EXTENSIONS "parquet")');
  });

  it('always includes core_functions', () => {
    const result = run(['--extensions', 'parquet']);
    expect(result.stderr).toContain('Adding core_functions');
    expect(generatedConfig()).toContain('duckdb_extension_load(core_functions)');
    expect(generatedConfig()).not.toContain('SKIP_EXTENSIONS');
  });

  it('pins out-of-tree extensions to a commit', () => {
    run(['--extensions', 'httpfs']);
    expect(generatedConfig()).toMatch(
      /duckdb_extension_load\(httpfs GIT_URL \S+ GIT_TAG [0-9a-f]{40}/
    );
  });

  it('rejects unknown extensions', () => {
    const result = run(['--extensions', 'nonsense']);
    expect(result.status).toBe(1);
    expect(result.stderr).toContain("Unknown extension 'nonsense'");
  });

  it('fails when the DuckDB source is missing', () => {
    fs.rmSync(path.join(duckdbDir, 'CMakeLists.txt'));
    const result = run(['--extensions', 'json']);
    expect(result.status).toBe(1);
    expect(result.stderr).toContain('DuckDB source not found');
  });

  it('discovers the list from the app root', () => {
    const app = path.join(workDir, 'app');
    writeApp(app, ['json']);
    expect(run(['--app-root', app]).status).toBe(0);
    expect(generatedConfig()).toContain('duckdb_extension_load(json)');
  });

  it('prefers --extensions over the app package.json', () => {
    const app = path.join(workDir, 'app');
    writeApp(app, ['json']);
    run(['--app-root', app, '--extensions', 'icu']);
    const config = generatedConfig();
    expect(config).toContain('duckdb_extension_load(icu)');
    expect(config).not.toContain('duckdb_extension_load(json)');
  });

  it('includes core_functions when no extensions are configured', () => {
    const result = run(['--app-root', workDir]);
    expect(result.status).toBe(0);
    expect(generatedConfig()).toContain('duckdb_extension_load(core_functions)');
  });
});

// DuckDB's own CI records the extension commit each release builds against.
// A DuckDB bump that misses one of these fails here rather than at build time.
describe('out-of-tree pins', () => {
  it.each(['sqlite_scanner', 'httpfs', 'fts', 'vss'])('%s matches the pinned DuckDB', (name) => {
    const upstream = fs.readFileSync(path.join(DUCKDB_CONFIG, `${name}.cmake`), 'utf8');
    const [, tag] = upstream.match(/GIT_TAG\s+([0-9a-f]{40})/);
    run(['--extensions', name]);
    expect(generatedConfig()).toContain(`GIT_TAG ${tag}`);
    expect(generatedConfig().includes('APPLY_PATCHES')).toBe(upstream.includes('APPLY_PATCHES'));
    expect(generatedConfig().match(/SUBMODULES \S+?(?=[\s)])/)?.[0]).toBe(
      upstream.match(/SUBMODULES \S+/)?.[0]
    );
  });
});
