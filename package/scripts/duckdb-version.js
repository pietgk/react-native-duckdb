#!/usr/bin/env node
// Prints the version or commit DuckDB's build expects when it has no .git to read.
const { version, commit } = require('../duckdb.json');

process.stdout.write(process.argv[2] === 'commit' ? commit : version);
