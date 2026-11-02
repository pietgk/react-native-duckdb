# Upgrading to DuckDB 2.0

This package moved from DuckDB 1.4 to 2.0. Existing databases keep working, but some SQL changes meaning. The [DuckDB migration guide](https://duckdb.org/docs/2.0/operations_manual/migration) has the full list.

## Your data

- Databases created by 1.4 open and work on 2.0. This was tested by upgrading an app in place
- **Rebuild full-text search indexes built by 1.4.** Searches on them can run out of memory on 2.0. See [below](#rebuild-full-text-search-indexes)
- Those files keep their storage format when 2.0 writes to them, so older versions can still read them. Nothing upgrades them automatically, and most apps do not need to
- **New files created by 2.0 cannot be read by older versions.** To share a file with an older DuckDB, see [Which format to use](#which-format-to-use)

### Rebuild full-text search indexes

An FTS index keeps its search macro in the database file, so an index built by 1.4 keeps the 1.4 macro, and 2.0 runs that macro much less efficiently. This does not depend on the storage format, and upgrading the file does not fix it, because the old macro is copied with the file.

Rebuild each index once after moving to 2.0, with the options you created it with:

```ts
db.executeSync("LOAD 'fts'")
db.executeSync("PRAGMA create_fts_index('docs', 'id', 'title', 'body', overwrite = 1)")
```

These are the results for one search, with a file created by DuckDB 1.4.5, on the default 256 MB limit:

| Documents | Term found in | 1.4.5 | 2.0, 1.4 index | 2.0, rebuilt |
|---|---|---|---|---|
| 20,000 | 1% of documents | 47 MB, 0.04 s | 290 MB, 0.23 s | 49 MB, 0.05 s |
| 20,000 | Every document | 55 MB, 0.05 s | Out of memory | 57 MB, 0.06 s |
| 200,000 | 1% of documents | 69 MB, 0.11 s | Out of memory | 74 MB, 0.06 s |

The figures are peak memory and time for the DuckDB CLI on an M1 Pro. The out of memory results and the fix were confirmed with this package on the iOS simulator. Raising the limit to 1 GB only moved the failure: the two failing searches then took over 70 seconds.

### Which format to use

Keep the default unless a file must be read by an older DuckDB. Before 2.0, DuckDB created `v1.0.0` files unless told otherwise, so that is what a file from an app built on 1.4 uses.

| File format | 1.4 reads it | 1.5 reads it | 2.0 reads it |
|---|---|---|---|
| `v1.0.0` | Yes | Yes | Yes |
| `v1.4.0` | Yes | Yes | Yes |
| `v1.5.0` | No | Yes | Yes |
| `v2.0.0`, the default | No | No | Yes |

To share a file with an older DuckDB, such as a server still on 1.4, set the format when you create the file. The option has no effect on a file that already exists. DuckDB supports 1.4 until 17 November 2026.

```ts
const db = HybridDuckDB.open(path, { storage_compatibility_version: 'v1.4.0' })
```

To see which format a file uses:

```ts
const [{ version }] = db
  .executeSync("SELECT tags['storage_version'] AS version FROM duckdb_databases() WHERE NOT internal")
  .toRows()
// 'v1.4.0+'
```

An older DuckDB refuses a newer file with an error such as `Trying to read a database file with version number 999, but we can only read versions between 64 and 67`.

### What the 2.0 format changes

DuckDB says the 2.0 format loads column metadata lazily, enables `DICT_FSST` compression by default, stores deletions more compactly and validates data more strongly. DuckDB publishes no figures, so we measured the same data in each format. Files in the `v1.4.0` format were written by 2.0, which already uses `DICT_FSST` for them.

| Data | Measure | File from 1.4 (`v1.0.0`) | That file upgraded | `v1.4.0` | `v2.0.0` |
|---|---|---|---|---|---|
| 1.9 million rows, 11 columns | File size | 98.3 MB | 100.1 MB | 98.6 MB | 98.6 MB |
| | Open | 0.3 ms | 0.4 ms | 0.2 ms | 0.1 ms |
| 200,000 rows, 1,001 columns | File size | 325.6 MB | 321.9 MB | 321.1 MB | 321.1 MB |
| | Open | 8.0 ms | 1.3 ms | 1.2 ms | 1.4 ms |
| | Memory after opening | 5.1 MB | 2.1 MB | 2.0 MB | 2.1 MB |
| 2.8 million rows after deleting 30% of 4 million | File size | 2.1 MB | 1.6 MB | 2.1 MB | 1.8 MB |

The figures are medians of 25 runs in new processes with the DuckDB CLI on an M1 Pro, with the files already in the disk cache and the time and memory of starting an empty process removed. We did not measure on a phone, which will be slower. File sizes are rounded up to 256 KB blocks, so the last row is rough, and the upgraded file there is smaller partly because copying drops the deleted rows.

So only very wide tables gain anything noticeable, and file size barely changes. Upgrading is optional.

### Upgrading a file to the 2.0 format

Copy the database into a new file. 2.0 creates the new file in the 2.0 format and does not change the old one, so if anything fails you still have your data. You need free space for a second copy.

```ts
const oldPath = `${DOCUMENTS_PATH}/app.duckdb`
const newPath = `${DOCUMENTS_PATH}/app_v2.duckdb`

// Removes anything left by an earlier attempt that was interrupted
HybridDuckDB.deleteDatabase(newPath)

const db = HybridDuckDB.open(oldPath, {})
try {
  db.executeSync("LOAD 'vss'") // Only needed if the file has HNSW indexes
  db.attach(newPath, 'upgraded')
  try {
    // `app` is the old file's name without its extension
    await db.execute('COPY FROM DATABASE app TO upgraded')
  } finally {
    db.detach('upgraded')
  }
} finally {
  db.close()
}
```

Then:

1. Open `newPath` and check it, for example by comparing row counts with the old file.
2. [Rebuild any full-text search indexes](#rebuild-full-text-search-indexes) in the new file, because the copy keeps the old macro.
3. Save `newPath` as the path your app opens, for example in your settings storage. Do this before removing anything, so an app killed at this point still opens a complete file.
4. Call `HybridDuckDB.deleteDatabase(oldPath)`.

The package has no call to rename a file. To keep the original name, close the database and move the file with a file system library such as `expo-file-system`.

The copy needs more memory than normal use. On the default 256 MB limit, a 200,000-row table copied at 500 columns but failed with `Out of Memory Error` at 750. 512 MB was enough at 750 columns and 1 GB at 1,001. A failed copy leaves the old file unchanged, and you can run it again after raising the limit with `SET memory_limit`. The 1.9 million rows above took about a second on an M1 Pro with a 315 MB peak.

We do not upgrade files automatically when opening them, because it would be slow and use a lot of memory at the moment your app starts, for little gain.

## Queries that need changes

- **Full-text search:** you can no longer reference a `match_bm25` alias in the same query's `WHERE`. Move it into a subquery:

  ```sql
  SELECT * FROM (
      SELECT *, fts_main_docs.match_bm25(id, 'query') AS score FROM docs
  ) WHERE score IS NOT NULL ORDER BY score DESC;
  ```

- **Lambdas:** `x -> x + 1` is off by default. Use `lambda x : x + 1`
- **`~` and `!~`:** these now match anywhere in the string. `'abc' ~ 'b'` is now true
- **Division:** integer `//` and `%` by zero now throw instead of returning `NULL`. Use `TRY(1 // 0)` for `NULL`
- **Untyped `NULL`:** `CREATE TABLE t AS SELECT NULL AS x` now makes a column of type `NULL`. Write `NULL::VARCHAR AS x`
- **`X'...'` and `B'...'`:** these now return `BLOB` and `BITSTRING`
- **S3 URL parameters:** these are off by default. Use secrets instead

## This package

- `stream()` and `streamNamed()` take a single statement
- `getProfilingInfo()` JSON now has an `operator` key where it had `children`
- `EXPLAIN` shows an index scan as `Hnsw Index Scan`, where it showed `HNSW_INDEX_SCAN`
- The build now needs Python 3 as well as git
- HTTPS certificates are now verified against the operating system's roots, so a server with an invalid certificate is refused
