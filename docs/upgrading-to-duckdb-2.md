# Upgrading to DuckDB 2.0

This package moved from DuckDB 1.4 to 2.0. Existing databases keep working, but some SQL changes meaning. The [DuckDB migration guide](https://duckdb.org/docs/2.0/operations_manual/migration) has the full list.

## Your data

- Databases created by 1.4 open and work on 2.0, including their indexes. This was tested by upgrading an app in place
- Those files keep their storage version when 2.0 writes to them, so older versions can still read them
- **New files created by 2.0 cannot be read by older versions.** To keep them readable, set `storage_compatibility_version` when you open the database

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
