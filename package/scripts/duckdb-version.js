#!/usr/bin/env node
// Prints the version string DuckDB's build expects when it has no .git to read.
const { version, commit } = require('../duckdb.json');

process.stdout.write(`${version}-0-g${commit.slice(0, 10)}`);
