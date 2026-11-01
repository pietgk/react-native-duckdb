# Review and status, 2026-10-30

This document records the review of the fork `pietgk/react-native-duckdb` and the first run of its example app. It sets out an ordered path: fix the issues, decide on the 3 upstream PRs, reach the latest Expo SDK, improve testing, and decide whether the fork is worth maintaining.

The order matters. Each step builds on the one before it, so nothing conflicts or has to be redone.

Decisions already made:

- The fork is independent, with its own scoped package. Small general fixes are still offered upstream.
- `example/` is converted to Expo, not duplicated.
- PR #2 (spatial) is deferred.
- Web gets a short assessment only.

Evidence labels used below:

- **Reproduced**: seen happening on this machine.
- **Confirmed by inspection**: checked directly in the repo, the published tarball, or a registry.
- **Read from code**: a reading of the source. Not yet triggered at runtime.

Section 10 records what has been done since this review was written. Sections 1 to 9 are the original review.

## 1. Status at 2026-10-30

**What runs:** the iOS simulator. A clean `pod install` and build succeed, the in-app suite reports 149 passed and 0 failed, and all tabs and Explorer screens opened. `tsc --noEmit` passes in both `example/` and `package/`.

The suite registers 153 tests. The 4 in `example/src/tests/benchmark.test.ts` are off by default, which gives the 149.

**Not tested:** Android, a physical device, a release build, and `npm install` by a consumer.

**Uncommitted work:** 13 changed files on `main`. They need a branch and commit first.

| Area | Files |
|---|---|
| fmt workaround | `example/ios/Podfile` |
| Link flags | `package/RNDuckDB.podspec` |
| Rebuild fix | `scripts/build-duckdb-ios.sh` |
| Remote Files | `example/src/screens/RemoteFilesScreen.tsx` |
| Types | `example/tsconfig.json`, unused `example/src/screens/TestSuiteScreen.tsx` removed |
| UI | `ResultTable.tsx`, `DatasetStack.tsx`, `AboutScreen.tsx`, `DatasetDetailScreen.tsx`, `DatasetExplorerScreen.tsx`, `QueryRunnerScreen.tsx`, `colors.ts` |

`git status` also shows the `duckdb` submodule as having untracked content. That is build output, not a change to commit. PR #1 silences it.

**Environment:** Xcode 26.6, bun 1.4.2 (the repo pins 1.3.1), Node 24.15, Homebrew `pod` 1.16.2, Maestro via `mise x`. The Android SDK is at `/opt/homebrew/share/android-commandlinetools`, with `ANDROID_HOME` unset and `adb` not on `PATH`.

**Correction to earlier notes:** the 3 PRs are on upstream (`pranshuchittora/react-native-duckdb`), authored by Shane98c. The fork has none. `gh` resolves to upstream in this checkout (`remote.upstream.gh-resolved = base`), which caused the mix-up. Pass `-R` or run `gh repo set-default` when the fork is meant.

## 2. Review findings

### Fixed in the working tree

| Finding | Evidence |
|---|---|
| fmt 11.0.2 does not compile on Xcode 26.6 clang | Reproduced |
| First `pod install` link failure: the podspec read a file generated later | Reproduced |
| Library doubled on each rebuild: simulator slice went from 99M to 197M in the build log (about 103 MB to 207 MB) | Reproduced |
| Remote Files ran `INSTALL httpfs`, giving HTTP 404 | Reproduced |
| Stray `)}` caused a runtime error toast and hid 6 type errors | Reproduced |
| About version, unreadable yellow text, raw route name in the back button | Reproduced |

### Open, highest priority first

| Finding | Location | Evidence |
|---|---|---|
| **Published npm package cannot build.** The 1.0.0 tarball is 852K with no `scripts/`, DuckDB source or `duckdb/` directory. `files` lists `scripts`, but `package/scripts` does not exist. The podspec calls `../scripts/build-duckdb-ios.sh` and CMake uses `../../scripts` | `package/RNDuckDB.podspec:19`, `package/android/CMakeLists.txt:16`, `package/package.json:31` | Confirmed by inspection of the tarball; install not attempted |
| **No LICENSE file**, though MIT is declared and the README badge links to one | repo root, tarball, `README.md:7` | Confirmed by inspection |
| **DuckDB update path is dead.** The workflow reads `VERSION` while the file is `DUCKDB_VERSION`, and it downloads the amalgamation into `package/vendor/duckdb`, which no build reads. Builds use the `duckdb` submodule, which the workflow never moves | `.github/workflows/update-duckdb.yml:20`, `scripts/download-duckdb.sh:17` | Confirmed by inspection |
| Appender `close()` swallows flush failures | `package/cpp/HybridAppender.cpp:232` | Read from code |
| `appendRows` takes the column count from the first row and never checks later rows, so longer rows are silently truncated | `package/cpp/HybridAppender.cpp:152` | Read from code |
| `attach` and `detach` build SQL by concatenation | `package/cpp/HybridDatabase.cpp:556`, `package/cpp/HybridDatabase.cpp:587` | Read from code |
| Raw `this` in async lambdas, and a `close()` race | `package/cpp/HybridDatabase.cpp:103`, `package/cpp/HybridStreamingResult.cpp:68` | Read from code; 8 UI attempts did not crash |
| `executeBatchSync` can leave a transaction open: `toValues` can throw outside any rollback | `package/cpp/HybridDatabase.cpp:612` | Read from code |
| `rolledBack: true` is reported even if the rollback failed | `package/src/transaction.ts:137` | Read from code |
| Extension change may not rebuild: CocoaPods skips `prepare_command` when the spec is unchanged. The documented recipe also uses paths that do not exist in the npm layout (`node_modules/react-native-duckdb/duckdb/`, `.../package/DuckDB.xcframework`) | `docs/bare-workflow.md:100` | Skip observed; recipe not run; paths confirmed by inspection |
| No working lint or unit tests: `lint` and `test` scripts exist, but eslint is not installed and `package/` has no test files. No PR CI: the only workflows are tag-triggered release and manual update. 0 `testID`s | `package/package.json:50`, `.github/workflows/` | Confirmed by inspection |
| Fork still has upstream identity: store bundle ID `com.rnduckdbexample`, Apple team `N2LHFY8LXC`, repo URLs, npm name `react-native-duckdb` (taken by upstream) | `example/ios/RNDuckDBExample.xcodeproj/project.pbxproj:272`, `example/src/screens/AboutScreen.tsx:51`, `package/package.json:71` | Confirmed by inspection |
| OpenSSL and curl hardcode iOS 15.1 instead of using `MIN_IOS` | `scripts/build-openssl-curl.sh:116` | Read from code |

## 3. The 3 upstream PRs

All 3 were opened on 2026-07-17 by Shane98c. None has a review. The only comment is a question from pietgk on #3 (2026-10-04), still unanswered.

These were reviewed by reading the diffs. None has been built here yet. `git apply --check` was run against the current working tree.

| PR | Size | Applies | Advice |
|---|---|---|---|
| #1 Android lib -13 MB | +17/-7 | Yes | **Include now** |
| #3 iOS extension discovery | +31/-3 | Yes | **Include now, with one change** |
| #2 spatial | +952/-19 | No | **Defer** |

**PR #1.** Low risk, and it adds a real CMake bug fix. Its `ignore = untracked` line also clears the `duckdb` noise in `git status`.

- Drop its `.gitignore` line for `package/.duckdb-extensions.json`, since that file no longer exists after our fix.
- Drop the stale GDAL/GEOS wording from its CMake comment.
- Keep Shane98c's authorship.
- It needs the first Android build to verify.

**PR #3.** Required for Expo. The current probes check `${REPO_DIR}/../ios` and `${REPO_DIR}/../../ios` (`scripts/build-duckdb-ios.sh:22`), which never reach `example/ios`, so `app.json` extensions would be silently ignored in this repo.

- Change: pass the path through `ENV` in the podspec, not inside `prepare_command`.
- Why: the `RNDuckDB` checksum in `Podfile.lock` is the SHA-1 of `Pods/Local Podspecs/RNDuckDB.podspec.json`, and that JSON embeds `prepare_command`. Confirmed on this checkout (`2a0ee7d`). An absolute path there would make the checksum differ per machine.
- To test when cherry-picking: that the `ENV` value reaches the `prepare_command` process.

**PR #2.** Deferred for these reasons:

- It adds a vcpkg and GDAL chain.
- It is pinned to the spatial commit matching DuckDB 1.4.4, so it needs re-pinning for DuckDB 2.0 anyway.
- It fails to apply on `package/RNDuckDB.podspec` and `scripts/build-duckdb-ios.sh`, because it extends the JSON file we removed.
- It is stacked on #1.

Worth reusing later: the `iconv` linkage and the SQLite feature test. It also found the library doubling bug independently.

**Order:** #1 then #3, as separate cherry-picks. They touch different files.

## 4. Fix plan, in order

Each milestone has an exit check and keeps the app green before the next begins.

**M0 Stabilise**

- Branch, commit the current fixes in logical commits, and add a LICENSE that preserves the original copyright
- Apply PR #1 and PR #3
- Bump DuckDB from 1.4.4 to 1.4.5 (released 2026-06-17). This is a stopgap: 1.4 LTS support ends 2026-11-17
- First Android run. Set `ANDROID_HOME` first. Platforms 36 and 37.0, NDK 27.1.12297006 and an android-36 system image are installed
- Exit: 149 pass on iOS and Android

**M1 Safety net** (section 6)

- Exit: a broken build, type error or failing test blocks a PR

**M2 Native correctness**

- Fix the open C++ and TS findings, writing a failing test first for each
- Use `shared<>`-style lifetimes for async work and guard `close()` against running work
- Exit: new tests pass and old ones still pass

**M3 Packaging**

- Move scripts into the package and fetch a pinned DuckDB source tag at build time, so npm installs and EAS work without a git submodule. Do not go back to the amalgamation: DuckDB 2.0 deprecates it
- Keep the build entry point a plain script, not tied to `prepare_command`, so a future Swift Package Manager path can reuse it (section 5)
- Publish under a scoped name, update identity and docs, and correct the extension-change recipe
- Exit: a fresh app outside the repo installs from the packed tarball and passes a smoke test

**M4 Expo SDK 58** (section 5)

**M5 DuckDB 2.0 LTS**

- Target the latest 2.0.x. 2.0.0 shipped on 2026-10-21 and 2.0.1 is tentatively due 2026-11-18. 2.0 is supported until at least October 2027
- Repin all 4 out-of-tree extensions in `scripts/configure-extensions.js` (`sqlite_scanner`, `httpfs`, `fts`, `vss`) and replace the dead update workflow
- Compile first. The library uses DuckDB's C++ API directly, including internal headers, and the migration guide does not cover it. The new C++17 minimum is already met (the build uses C++20)
- Check against the suite:
  - ICU is no longer bundled in custom builds, and the `icu` extension now implements it natively
  - Single-arrow lambdas are off by default
  - `~` now does partial matching
  - Untyped `NULL` keeps the NULL type
  - Integer `//` and `%` by zero now throw
  - `X'...'` and `B'...'` literals return BLOB and BITSTRING
  - S3 URL parameters are off by default
- A text search of the tests, screens and docs found no single-arrow lambdas, `~` operators or bare `SELECT NULL`, so the main risk is in consumers' queries. Note the changes in the release docs
- Document storage: new files created by 2.0 cannot be read by older versions. Existing files keep their storage version when 2.0 writes to them, so they stay readable. `storage_compatibility_version` can keep new files readable
- If 2.0 stalls, DuckDB 1.5 is supported until March 2027 and is the fallback
- Exit: the suite passes, and a 1.4-created database opens correctly

**M6 Later:** revisit spatial. Web only if a real need appears.

## 5. Expo plan

Target: `expo` 58.0.21, `react-native` 0.88.1, `react` 19.3.0. Use Expo's pinned React version, not the npm `latest` (19.3.1).

SDK 58 requires Xcode 26.4+ (26.6 is installed), iOS 16.4+, Android 7+, compileSdk 37 and targetSdk 36.

**Gate first, as a time-boxed spike.**

- Stable Nitro 0.37.1 (2026-08-27) was developed against RN 0.85.3, and its `react-native` peer range is `*`, so installation will not warn.
- Nitro's "Upgrade React Native to 0.88.0" commit landed on 2026-10-19, with no stable release after it. Only `0.37.2-nightly` and `1.0.0-alpha.nightly` builds exist.
- That same week Nitro split its core and moved the React Native adapters out of it, so the next stable release may be a bigger step than a patch.
- Test Nitro 0.37.1 on RN 0.88. If it fails, use a newer stable release if one exists, otherwise temporarily target SDK 57 (RN 0.86, React 19.2.3) and move up later. Do not ship on a nightly.

**Sequence:** Nitro 0.33.9 to 0.37.x and regenerate `nitrogen/`, then RN and Expo, then convert the example. The custom `appendRow` raw JSI path needs specific checking.

**Convert `example/`:**

- Remove committed `ios/` and `android/`, and use `app.json` plus a dev client
- Keep React Navigation, screens and tests unchanged
- Replace deprecated `react-native-vector-icons` with `@expo/vector-icons`
- Align versions with `expo install --fix`
- Use our own bundle IDs

**The fmt workaround goes away.** RN 0.85 and later ship fmt 12.1.0, including 0.88.1, and a generated Podfile cannot hold the workaround anyway. Keep it until M4, and confirm by building without it.

**Plugin:** move from `@expo/config-plugins` ^9 to the SDK 58 version. The APIs it uses (`createBuildPodfilePropsConfigPlugin`, `withGradleProperties`, `createRunOncePlugin`) still exist in 58.0.11. `extensions` moves from `package.json` to `app.json`.

**Risks to test:**

- Precompiled RN and Expo modules are on by default in the SDK 58 template Podfile. Check they link with the Nitro and DuckDB pod. Opt-outs are `ios.buildReactNativeFromSource` and `EXPO_USE_PRECOMPILED_MODULES`
- Hermes V1: the template treats it as on unless `expo.useHermesV1` is `false`. Expo's release notes do not mention it, so confirm which engine actually runs
- Expo autolinking must pick up the workspace package
- EAS builds need the DuckDB source (M3) and take about 8.5 minutes of native compile, measured once on this Mac, so caching matters
- The EAS `sdk-58` image uses Xcode 27.1, not the local 26.6. A local pass does not prove the EAS build, so run an EAS build early

**Bare React Native** keeps coverage through a small CI build check.

**Longer term: CocoaPods.** This needs design care in M3 but no migration work yet.

- React Native 0.88 keeps CocoaPods fully supported. Its stated plan targets Swift Package Manager as the default in 0.89, with a CocoaPods opt-out, and removes CocoaPods in a later release
- The DuckDB build runs from the podspec `prepare_command`. The RN post does not cover that case, so this is the main long-term build risk
- CocoaPods trunk is due to go read-only on 2026-12-02 and has refused new pods using `prepare_command` since May 2025. We do not publish to trunk, so neither affects builds directly, but they show where this mechanism is heading

**Exit:** a dev client builds on both platforms from clean, the plugin's extension list is proven honoured, and the full suite passes.

## 6. Testing advice

Root cause: the 149 tests are good, but they only run when someone taps Run All, only after a successful build, and they call the library directly rather than the screens. Nothing fails automatically.

| What we found | Why it was missed | What will catch it |
|---|---|---|
| fmt and link failures | Nothing runs a clean build | Clean build in CI, nightly without cache |
| `INSTALL httpfs` | Tests use `LOAD`, screens are not tested | A Maestro flow per screen |
| Stray `)}` | Babel accepts it, `tsc` was not enforced | Typecheck gate, fail on LogBox errors |
| Library doubling | Only visible on a rerun | Build twice, compare size and member count |
| npm package gaps | Never installed as a consumer | Pack-and-install fixture |
| Wrong version, contrast, route name | No visual checks | Maestro screenshots plus review |

Use what exists:

- Keep the in-app suite as the native contract test
- Add a launch-flag autorun that calls `TestRunner.runAll()` (`example/src/testing/TestRunner.ts:40`) and writes a JSON summary, so CI gets pass or fail
- Merge the benchmark filter from `example/src/screens/TestsScreen.tsx:65` into the runner, so there is one code path

Add:

- `testID`s and Maestro flows. Start from the 6 flows written during this review. They are in `/tmp/rndb-flows`, outside the repo, so copy them in early in M1 before they are lost
- A rule that any error card or LogBox entry fails a flow
- Tags on network tests, running them nightly because they depend on third-party URLs
- ESLint and Jest for logic that needs no device: `configure-extensions.js`, the config plugin, `DuckDBError`, transactions
- Tests for each open finding: failed appender close, mismatched rows, apostrophe in a path, batch parameter failure, close during a query

**CI layout:**

- PRs run typecheck, lint, Jest, an iOS build with autorun, and an Android emulator run. The DuckDB build is cached by submodule SHA, extension list and script hash
- Nightly runs a clean build, Maestro and the network tests

React Native Harness (Callstack) offers Jest-style tests on devices, and Nitro moved its runtime tests to it in October 2026. Worth evaluating after M1, but not a reason to rewrite 149 working tests now.

## 7. Maintenance advice

**Recommendation: yes, maintain it, on condition that M0 and M1 land first.**

- **For:** upstream has had no commits since 2026-02-22 (UTC), and the PRs have gone unreviewed since July, so waiting is not a plan. The native layer is small (the largest file is 661 lines) and MIT licensed. The feature set worked in practice: remote Parquet, FTS, HNSW and Hugging Face schemas.
- **Against:** upkeep is real. Expo shipped 4 SDKs in about 7 months (55 on 2026-02-25 to 58 on 2026-09-29), plus Nitro and DuckDB releases. Native builds are heavy, and much of the work would rest on one maintainer.
- **Cost control:** track Expo every other release at most, stay on DuckDB LTS, and add few extensions.
- **Stop or reassess if:** Nitro or the move off CocoaPods demands a rewrite, or an officially supported DuckDB option for React Native appears.
- **As a reference example:** it already suits this well. Add a table mapping each feature to its screen, test and docs page. Use a distinct app name and bundle ID, since the "DuckDB Explorer" store listings belong to upstream.
- **Upstream:** offer M0 and M2 fixes as small PRs, and note in the README that this is a maintained fork.

## 8. Web note

Native cannot share with web: browsers would use DuckDB-WASM behind a shared TypeScript interface. Synchronous APIs (`executeSync`) do not map cleanly to an async worker, so the shared surface would be async only. Native extensions, file paths and the appender would differ.

No work is planned. Revisit once a real app needs it.

## 9. Sources

Expo:

- [SDK 58 changelog](https://expo.dev/changelog/sdk-58)
- [SDK version requirements](https://docs.expo.dev/versions/latest/)
- [How to upgrade to SDK 58](https://expo.dev/blog/how-to-upgrade-to-sdk-58)

DuckDB:

- [Announcing DuckDB 2.0.0 LTS](https://duckdb.org/2026/10/21/announcing-duckdb-200)
- [Migrating to DuckDB 2.0](https://duckdb.org/docs/2.0/operations_manual/migration)
- [Release calendar](https://duckdb.org/release_calendar)

React Native and CocoaPods:

- [React Native 0.88 release post](https://reactnative.dev/blog/2026/10/19/react-native-0.88)
- [Swift Package Manager RFC](https://github.com/react-native-community/discussions-and-proposals/pull/994)
- [CocoaPods trunk read-only plan](https://blog.cocoapods.org/CocoaPods-Specs-Repo/)

Nitro and testing:

- [Nitro releases](https://github.com/margelo/nitro/releases)
- [React Native Harness introduction](https://callstack.com/blog/introducing-react-native-harness-fast-real-device-testing-for-native-modules)

Upstream PRs:

- [#1 shrink native Android lib](https://github.com/pranshuchittora/react-native-duckdb/pull/1)
- [#2 spatial extension support](https://github.com/pranshuchittora/react-native-duckdb/pull/2)
- [#3 iOS extension discovery](https://github.com/pranshuchittora/react-native-duckdb/pull/3)

## 10. Progress

M0 to M5 are merged to `main` on the fork. [`v2.0.0`](https://github.com/pietgk/react-native-duckdb/releases/tag/v2.0.0) is published and contains all of them. [`v1.0.0`](https://github.com/pietgk/react-native-duckdb/releases/tag/v1.0.0) predates M4 and M5.

| Milestone | State | Result |
|---|---|---|
| M0 Stabilise | Done | Fixes committed, LICENSE added, PR #1 and PR #3 applied with the agreed changes, DuckDB 1.4.5. First Android run passed |
| M1 Safety net | Done | ESLint, Jest, autorun harness, testIDs, Maestro flows and workflows are in place. `main` requires `checks`, `ios` and `android` |
| M2 Native correctness | Done | Every open native finding reproduced with a failing test, then fixed |
| M3 Packaging | Done | The packed tarball installs in a new app outside the repo, builds DuckDB from nothing and passes a smoke test on iOS and Android. `v1.0.0` released |
| M4 Expo SDK 58 | Done | Example converted: native projects are generated from `app.json`. Suite passes on both platforms. EAS not tried |
| M5 DuckDB 2.0 LTS | Done | A real 1.4 database opens on 2.0 after an in-place app upgrade on both platforms, with identical results. Full suite passes |
| M6 onward | Not started | Not needed for 2.0 |

Verified on the `v2.0.0` code. CI and nightly ran on the last branch commit, and the merge to `main` has the same tree:

| Check | Result |
|---|---|
| Typecheck, ESLint, Jest (30 tests) | Pass |
| CI, iOS simulator, no network tests | 160 passed, 0 failed |
| CI, Android emulator, no network tests | 160 passed, 0 failed |
| Nightly, iOS simulator, with network tests | 170 passed, 0 failed |
| iPhone 16e, Release build, with network tests | 170 passed, 0 failed |
| Maestro flows on iOS | 4 of 4 pass |
| iOS rebuild over the previous output | Identical: 133,866,376 bytes, 1851 members |
| Published tarball in a new app, iOS and Android | Smoke test passes on both, built from the public URL with no credentials |
| Extension change recipe | Followed on both platforms before M4. A plain `pod install` misses the change, the documented steps rebuild, and a repeat with no change skips in 12 seconds |

The harness was checked for false passes. A deliberate failing test exits 1 on both platforms, and the two original bugs (stray `)}` and `INSTALL httpfs`) were reintroduced and caught. The second showed a blind spot, now closed: an error card can be cleared by the next query, so flows check when a screen opens.

### Distribution decision

The package is not published to npmjs.com, so there is no npm package to maintain. A `v*` tag publishes one tarball to two places on GitHub:

| Path | Install | Tradeoff |
|---|---|---|
| GitHub Release asset | URL, no token | No version ranges or `npm update` |
| GitHub Packages | By name, with a `read:packages` token | GitHub requires a token even for public packages, so every consumer and EAS build needs one |

The Release asset is the default in the docs because it needs no setup. npm provenance is replaced by a GitHub attestation on the tarball.

`v1.0.0` verified from the published asset: anonymous download, attestation passes and matches the downloaded file, and a new iOS app built from the URL with no credentials passes the smoke test. GitHub Packages was published to, but installing from it with a token has not been tried. The package came out public because it is linked to this public repository.

`v2.0.0` verified the same way, and on Android as well: anonymous download, attestation passes, a token-free `npm install` works, and new apps built from the URL on iOS and Android pass the smoke test.

### Expo decisions

- The spike passed, so there was no need for the SDK 57 fallback or a nightly: stable Nitro 0.37.1 works on React Native 0.88.1 and SDK 58 with precompiled React Native on iOS. Nitro 0.37 needs React Native 0.85 or later on Xcode 26, because its module pulls in the fmt that 0.82 pins
- Hermes V1 is confirmed at runtime: `HermesInternal` reports Static Hermes
- The `fmt` workaround is gone. Nothing replaces it
- The example builds without `expo-dev-client`, a change from section 5. A Debug build loads Metro directly, and a launcher screen would block hands-free CI runs. Add it if someone wants a shareable development build
- Bare React Native stays covered by the install test, which builds a bare 0.88 app. It passes on iOS and Android

### DuckDB 2.0 notes

- The migration guide does not cover the C++ API, which changed a lot. `MaterializedQueryResult` and `PendingQueryResult` are gone, replaced by one `QueryResult` plus `QueryResultStream`. The port removed 94 lines, because four copies of the progress loop became one helper
- Extensions no longer come with `duckdb_static`. Each has its own archive, and `duckdb_register_static_extensions()` must run before a database opens. Our C++ calls it on both platforms. On iOS the build compiles DuckDB's generated loader into the combined archive
- `fts` now needs `APPLY_PATCHES` and `sqlite_scanner` needs `SUBMODULES`. Jest compares both with DuckDB's config, as it does the commits
- The version must be the plain tag. `v2.0.0-0-g...` left DuckDB's version numbers empty
- `scripts/update-duckdb.js` replaces the dead update workflow. It reproduced these pins exactly
- `docs/upgrading-to-duckdb-2.md` lists what changes for consumers

### Corrections to the review

- `close()` during a query was labelled "Read from code; 8 UI attempts did not crash". It hung the app when tested directly.
- PR #1 was verified by the first Android build: `libduckdb.so` is no longer packaged.
- The build depended on a bun version. The pinned bun 1.3.1 defaults to the isolated linker, which removes paths the native builds use. `bunfig.toml` now requires the hoisted linker.
- The 1.4.5 bump needed a new `httpfs` pin. A Jest test now compares all pins with DuckDB's own config.

- Android results before the `-UDEBUG` fix were overstated. 11 FTS tests skipped silently and counted as passed, so "149 passed" on Android did not mean 149 tests ran.

### Found since the review

| Finding | Outcome |
|---|---|
| A small ASOF join crashed every Android debug build with SIGABRT | Fixed with `-UDEBUG`. `-DNDEBUG` does not undo DuckDB's `-DDEBUG`, which enables checked casts, and one fails on a hash inside a destructor. Debug variants only. Adapted from mongrov/react-native-duckdb@c74170b |
| `RNDuckDBInitProvider` was never registered: AGP 7.3+ reads the empty `AndroidManifestNew.xml` | Fixed. Paths were only set by a JNI_OnLoad fallback that uses a hidden API, so the failure reported in that fork did not reproduce here. Paths, persistence and cold start are unchanged |
| Release builds failed without `keystore.properties`: empty passwords against `debug.keystore` | Fixed. Falls back to debug signing |
| `await stream.start()` could resume before any chunk arrived. CI found this on its slower emulator | Fixed. If the worker finished before `start()` returned, the JS promise was created already resolved. It now settles through the JS dispatcher, behind the chunks. Reproduced locally at about attempt 100, then 20 of 20 runs clean |
| About a dozen open connections could kill the app. CI found this as `HermesGC: OOM` with 6.7 GB reported against an 8 MB JS heap | Fixed. Every database and connection reported 256 MB, and Hermes counts that against its heap limit. Reproduced with 30 connections. Now 8 MB for a database and 64 KB for a connection |
| The "Android rowid bug" in the docs was our own build, not DuckDB. `Information loss on integer cast` was the check `-DDEBUG` enabled | Workaround removed: 11 skipping tests, the "FTS Unavailable on Android" screen and the doc warnings. A failure now fails |
| An explicit extension list without `core_functions` silently dropped `sum()` and similar | Fixed. It is always linked |
| RN 0.82 on Xcode 26 needs the `fmt` workaround in every consumer's Podfile, after `react_native_post_install` | Documented in `docs/bare-workflow.md`. Goes away with M4 |
| `cancel()` was lost if the query had not started yet, because DuckDB clears the interrupt flag at query start. The query then ran to completion | Fixed. Work is counted when scheduled, and `cancel()` and `close()` share one routine that keeps interrupting until that work ends |
| `fontFamily: 'monospace'` only exists on Android, so iOS never showed the intended monospace | Fixed with a shared `monoFont` |
| HTTPS failed on Android with DuckDB 2.0, which verifies certificates but only looks in Linux locations | Fixed. Android joins the system roots into a bundle at startup. iOS builds curl 8.23 with Apple's trust store. A permanent test checks that self-signed and expired certificates are refused. Network tests only run nightly, so a no-network test now checks the Android bundle on every PR |
| Timing-dependent tests failed at random: HNSW speed and progress callbacks | Fixed. The HNSW test checks the query plan, and the progress tests use a larger table |
| Tests assumed connections they discarded stayed open, which failed in release builds | Fixed. Garbage collection removes such connections |
| The nightly rebuild check reported `rebuilding changed the library`: 361 members instead of 1851 | Fixed. The check did not pass the app's directory, so the rebuild used the default extension list. The library itself was fine |

### Open

- The Android package install test is manual. Nightly covers iOS only
- EAS builds have not been tried. The `sdk-58` image uses Xcode 27.1, which this package has not been built on
- Maestro on Android: the adb connection to the emulator drops when Maestro connects, with a second emulator running on this Mac. Nightly CI runs Maestro on iOS only until this is understood
- No physical Android device has been run. The iOS HTTPS risk is closed: the simulator can read the host Mac's `/etc/ssl/cert.pem`, so only a real device proves Apple's trust store path, and the iPhone 16e run passed
- Installing from GitHub Packages with a token has not been tried. It was agreed this is not needed, since the docs lead with the Release asset
