# Spatial baseline: proven basics for every app, opt-in geometry math

**Status:** not started. Replaces "M6: revisit spatial" in [the review](../review/2026-10-30-review-and-status.md).
**Checked:** 2026-11-01 on DuckDB v2.0.0, `main` at `c4b1588`. Recheck line numbers and pins before starting.

## Goal

Give apps built with agents a spatial baseline, so they do not reinvent it and can see it proven before relying on it. The example app is the use case. The baseline covers storing and showing shapes, filtering by map area, and geometry math.

## Decisions

| Decision | Why |
|---|---|
| Split into two levels | DuckDB 2.0 has `GEOMETRY` in core. Most basics already work without the spatial extension. |
| Level 1, basics: for every app | Uses the `parquet` and `json` extensions already linked. No new native code, size cost or licence issue. |
| Level 2, math: opt-in per app | Needs GEOS, which is LGPL-2.1. Apps take that on only by asking. |
| Compile the extension in | Runtime `INSTALL` is not possible on mobile. |
| Build it without GDAL | GDAL is the largest dependency and the riskiest parser, and a native crash kills the app (see [#887](https://github.com/duckdb/duckdb-spatial/issues/887), [#882](https://github.com/duckdb/duckdb-spatial/issues/882)). GeoJSON and GeoParquet cover the common formats. |
| Keep GEOS | General `ST_Intersects`, `ST_Contains`, `ST_Within`, `ST_Buffer`, `ST_Union` and `ST_IsValid` come from it. Native versions cover only box and point-in-polygon cases, and upstream CI does not test `SPATIAL_USE_GEOS=OFF`. |
| Keep WKT strings as the `GEOMETRY` result | This is today's behaviour, so nothing breaks. Binary access is `ST_AsWKB(geom)`. |
| Stay source-only | Prebuilt dependencies would shorten builds, but would make this project a distributor of LGPL binaries. |

Level 2 is provisional until Phase 2 measures it. Full GDAL support is only worth revisiting if an app must read GeoPackage, KML, GML or FlatGeobuf on the device.

## What works today

Run through the example app's Query screen on the iOS simulator, on a build with 8 extensions and no spatial.

| Capability | Query | Result |
|---|---|---|
| Type | `'POINT(1 2)'::GEOMETRY` | Works, returns `POINT (1 2)` |
| WKT, WKB | `ST_AsText`, `ST_AsWKB`, `ST_GeomFromWKB` | Works, WKB is a BLOB |
| Nested | `['POINT(1 2)'::GEOMETRY]` | Works, JSON with WKT strings |
| Map-area filter | `WHERE g && 'POLYGON(...)'::GEOMETRY` | Works |
| CRS | `ST_SetCRS`, `ST_CRS` | Works |
| GeoParquet | `COPY`, then `read_parquet` with `&&` | Works, file has `geo` metadata |
| GeoJSON values | `ST_AsGeoJSON`, `ST_GeomFromGeoJSON` | Works |
| GeoJSON files | `COPY ... (FORMAT geojson)`, `SELECT * FROM 'x.geojson'` | Works |
| Point math | `vertex_extract(g, 'X')` with plain SQL | Works |
| Polygon math | `ST_Area`, `ST_Contains`, `ST_Distance` | Fails, needs spatial |
| Parse by name | `ST_GeomFromText` | Fails, use the `::GEOMETRY` cast |
| Install | `INSTALL spatial` | Fails, Permission Error |

Not covered by this check: Android, real devices, performance, bbox pruning, `getColumn()`, streaming, params and the Appender.

## Work

### Phase 1: basics, on a branch from `main`

- [ ] Fix `getColumn()` returning `""` for BLOBs (`package/cpp/HybridQueryResult.cpp:386`). Failing in-app test first. Found by reading code, not yet reproduced.
- [ ] Fix nested BLOBs emitting `\x`, which is invalid JSON (`package/cpp/json_serializer.hpp:92-101`). Failing in-app test first. Also not yet reproduced.
- [ ] Add `example/src/tests/spatial.test.ts` covering the table above, plus `getColumn`, streaming, WKB params, the Appender and a 1.4-created file (`src/common/types.cpp:1397` in DuckDB guards older storage).
- [ ] Add a `GEOMETRY` case to the result switches only if a test needs it. It currently falls to `default` (`HybridQueryResult.cpp:242`, `:599`).
- [ ] Add a Spatial Explorer screen next to `FTSExplorerScreen` and `VSSExplorerScreen`: load GeoJSON, filter by viewport, draw with the existing `react-native-svg`. This is the example agents copy.
- [ ] Add `docs/spatial.md`: what works without the extension and what needs it, each row naming its test, plus the traps below and a points-only "near me" recipe (bbox filter, then haversine in SQL). Link it from `docs/README.md` and `docs/types.md`.

### Phase 2: measure math, in a throwaway `spike/spatial` worktree

- [ ] Build spatial without GDAL, from a patched local copy via `SOURCE_DIR`, on iOS simulator and Android arm64.
- [ ] Build the same with GDAL for comparison.
- [ ] Measure both against baseline: stripped size raw and compressed, clean build time, `open()` latency, peak memory on `ST_Transform` and `ST_Buffer`.
- [ ] Run the full suite, including `sqlite_scanner` co-load and the GeoJSON overlap below.

### Phase 3: decide

- [ ] Record results and a go/no-go in `docs/review/`, and update the M6 line.
- [ ] If go, implement Level 2. Ask the maintainer before proposing a `SPATIAL_USE_GDAL` option upstream. The fallback is a carried patch, and since `APPLY_PATCHES` reads DuckDB's own tree, that needs a copy step or pinned fork.

## Level 2 costs and risks

- **Licence:** GEOS is linked into the app, so the LGPL obligation falls on whoever ships it. Closed-source apps need legal advice; the App Store discussion found dates from [2010-2011](https://lists.osgeo.org/pipermail/geos-devel/2011-April/005188.html). A dynamic GEOS build may help and needs a spike. The notice set for GDAL, PROJ and `proj.db` data is unverified.
- **Size:** baseline is 62.2 MB on Android arm64 (stripped debug, 21.3 MB gzip) and 81.8 MB for the iOS app binary. [PR #2](https://github.com/pranshuchittora/react-native-duckdb/pull/2) reports +24 MB raw and +8 MB compressed per arch with GDAL, on DuckDB 1.4.4. `proj.db` alone is about 8.5 MB, and CI has no size check.
- **Build:** consumers compile from source, so every first build, including EAS, gains GEOS and PROJ per slice, plus vcpkg if used. `proj_db.c` is a 51 MB source file. CI timeouts are 90 and 120 minutes, so caching is essential.
- **Runtime:** linked extensions load on every `open()`. GEOS and PROJ allocate outside DuckDB's 256 MB limit, so `memory_limit` will not prevent iOS memory kills.
- **Conflicts:** `sqlite_scanner` and PROJ both bring sqlite3, and the PR's `ar d sqlite3.c.o` fix is brittle. Spatial and `json` both register `ST_AsGeoJSON` and `ST_GeomFromGeoJSON`, so check they load together.
- **Maintenance:** each DuckDB upgrade also moves these dependencies. Upstream CI has no iOS or Android target, and `ST_Transform` axis order is announced to change.

## Traps

- The missing-function error says to run `INSTALL spatial`, which then fails on mobile.
- 3D WKT must be `POINT Z (1 2 3)`. `POINT(1 2 3)` fails.
- A geometry written to Parquet without a CRS read back as `GEOMETRY('OGC:CRS84')`, which is wrong for projected data.
- DuckDB's docs warn the on-disk geometry format is not yet stabilised.
- Maestro cannot find the Query screen's `Run` button by text, so tap by coordinates (about `87%,33%`). Queries over five lines grow the editor and move the button, so the tap misses and the screen keeps showing the previous result.

## Pickup notes

- **Reuse from [PR #2](https://github.com/pranshuchittora/react-native-duckdb/pull/2), keeping Shane98c's authorship:** triplets, SQLite superset build, `iconv` link, tests, docs.
- **PR #2 needs:** paths moved under `package/scripts`, a replacement for `.duckdb-extensions.json`, new inputs in the `BUILD_KEY` in `build-duckdb-ios.sh`, a fresh GDAL driver list, and `spatial` added to `scripts/update-duckdb.js:17` and `configure-extensions.test.js:120`.
- **Pins for v2.0.0:** spatial `4e066c17e2d1f24625baff4c62430d68ceb4e41f`, vcpkg `cd61e1e26a038e82d6550a3ebbe0fbbfe7da78e3`.
- **Unrelated:** `delta` looks broken. It is listed in-tree (`configure-extensions.js:17`), but `extension/delta` holds only a README in 2.0.

## Done when

- `spatial.test.ts` passes on iOS and Android in CI, and each BLOB fix has a test that failed first.
- The Spatial Explorer screen is exercised end to end on both platforms.
- `docs/spatial.md` lets an agent tell what works without the extension, without reading source.
- Level 2 has a recorded go/no-go with measured numbers.
