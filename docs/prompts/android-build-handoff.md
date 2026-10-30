# Prompt: continue the Android build investigation using relevant fork findings

You are working in `~/ws/react-native-duckdb`, continuing the current Android build/debugging work alongside an ongoing Expo migration. Use the findings below to identify and fix the actual failure, and prove that the example app works on Android. Prefer quality, robustness and maintainability over quick workarounds.

This handoff was prepared on 30 October 2026 from source/history inspection. The local snapshot was branch `m0-stabilise`, HEAD `429406a`, with substantial uncommitted native-correctness, test, CI and iOS-script changes. Re-read the current checkout before acting. The snapshot is context, not a target revision to restore.

## Scope and working rules

- Preserve all ongoing changes. Inspect `git status`, the current diff, relevant instructions and existing build processes first. Coordinate with any agent already using this checkout. Do not reset, stash, clean, overwrite or commit someone else's work.
- Start with the real end-user path: build, install and launch the example on the available Android emulator/device. If compilation prevents launching, preserve that build failure as the initial reproduction. Reuse an existing failing log or active build rather than starting another competing native build.
- Capture the exact command, first failing task, first meaningful compiler/linker/configuration error, ABI, variant, versions and log path. Diagnose that first error before reacting to subsequent cascading errors. Keep complete logs and real exit codes; do not hide failures behind `tail`, ignored status codes or pipelines without `pipefail`.
- Distinguish compilation/linking failures from runtime initialization and SQL failures. The two most useful Mongrov fixes below principally address runtime failures. They do not, by themselves, explain an arbitrary failed Gradle/CMake build.
- Follow the established Expo migration plan. Do not replace the repository with another fork or independently bump Expo, RN, Nitro, DuckDB, Kotlin, AGP or NDK to make one error disappear.
- Do not manually edit generated files, Nitrogen outputs or changelogs. Change source/configuration and use the appropriate generator. Keep changes narrowly connected to demonstrated failures.

## Current Android baseline to verify

At inspection, the example used RN 0.82.1, React 19.1.1, Nitro/Nitrogen 0.33.9 and DuckDB 1.4.5. The example was still a bare RN app; an Expo peer range or config plugin does not establish that its Expo conversion is complete.

Relevant configuration:

- `example/android/gradle.properties`: New Architecture and Hermes enabled; ABIs `arm64-v8a,x86_64`.
- `example/android/build.gradle`: minSdk 24, compileSdk/targetSdk 36, NDK `27.1.12297006`, Kotlin `2.1.20`. Library fallback properties differ from these app values, so inspect which values Gradle actually resolves.
- The repository uses Bun workspaces and a hoisted linker. Read `bunfig.toml`, the lockfile and existing install/CI instructions before changing dependency layout. The example's settings file resolves the RN Gradle plugin through the root `node_modules`.
- The example configures `core_functions,parquet,json,icu,sqlite_scanner,httpfs,fts,vss` in its `package.json`.
- `package/android/build.gradle` passes the consumer root and extension list into CMake and restricts its target to `RNDuckDB`.
- `package/android/CMakeLists.txt` generates the extension config, builds httpfs's OpenSSL/curl dependencies for 64-bit ABIs, adds the VSS API-24 allocation shim, and links DuckDB into the wrapper.
- The Android size reduction and iOS extension-discovery fixes from Shane98c were already incorporated as adapted local commits. Do not apply them again merely because their source SHAs differ.

## Priority source: Mongrov's Android initialization/debug fix

Read this exact commit and its two-file diff:

https://github.com/mongrov/react-native-duckdb/commit/c74170b2a0e092216336f82b74ab4c88d8e5a906

Title: `fix(android): register the init provider on modern AGP; build DuckDB without DEBUG`.

Mongrov's `main` matches upstream and does not contain the useful work. The commit is in the history of its non-default branches, including:

https://github.com/mongrov/react-native-duckdb/tree/wip/mongrov-restructure

The reviewed branch head was `59034473775aa46d1870fd1babc1eef3f9b11296`. Prefer commit-pinned source for reproducibility. Mongrov moved the library to its repository root; map its paths into our `package/` directory. Do not cherry-pick its entire restructuring history.

### A. Initialization provider missing from the selected manifest

Mongrov path: `android/src/main/AndroidManifestNew.xml`.

Our path: `package/android/src/main/AndroidManifestNew.xml`.

Observed locally: our modern manifest was empty. The provider was present only in `package/android/src/main/AndroidManifest.xml`. `supportsNamespace()` in our library's Gradle file selects the modern manifest for AGP 7.3+.

The existing provider class is `com.rnduckdb.RNDuckDBInitProvider`. Its `onCreate()` sends app-private file/database/external directories to `DocPathSetter.setAllPaths()`, which initializes native code and forwards the paths through JNI. Omitting it can leave the native default paths unset.

Mongrov adds the following inside `<application>` in the modern manifest:

```xml
<provider
    android:name="com.rnduckdb.RNDuckDBInitProvider"
    android:authorities="${applicationId}.rnduckdb.init"
    android:exported="false" />
```

Their author reports reproducing empty-path/default-open failures and verifying the fix on an arm64 API-34 emulator. Our matching source condition was confirmed; this handoff did not reproduce the runtime failure locally.

Reproduce using the app's normal default-path API, not only `:memory:` or an explicitly supplied absolute path. Inspect the actual merged manifest/APK, provider declaration/authority and native path initialization. Verify that files land in the app's own storage and that a database can be reopened after restarting the app. If native loading fails during provider startup, capture that independently rather than removing the provider to hide the error.

### B. DuckDB's DEBUG macro survives NDEBUG

Mongrov path: `android/duckdb-release-wrapper/CMakeLists.txt`.

Our path: `package/android/duckdb-release-wrapper/CMakeLists.txt`.

Observed locally:

```cmake
add_compile_options(-O3 -DNDEBUG)
```

Mongrov changes the DuckDB-scoped options to:

```cmake
add_compile_options(-O3 -DNDEBUG -UDEBUG)
```

`NDEBUG` and `DEBUG` are separate macros. Defining the former does not remove the latter. Their author reports DuckDB debug assertions causing an internal numeric-cast error during an ordinary ASOF join, followed by database invalidation.

Inspect the effective compiler command for the current DuckDB version, variant and ABI before adapting this. Check that `-UDEBUG` takes effect after any `-DDEBUG`, and that its scope is limited to DuckDB/extensions. Preserve wrapper/Nitro settings.

Do not globally force `CMAKE_BUILD_TYPE=Release` as a substitute. Mongrov reports that doing so caused duplicate `libNitroModules.so` artifacts in its build.

Run a representative ASOF join through the actual app API and confirm subsequent queries still succeed. A minimal semantic check is shown below, but it is not the author's exact failing query and may not trigger the assertion. Expand the reproduction using actual failing data/logs if necessary:

```sql
SELECT t.ts, p.price
FROM (VALUES (1), (3), (5)) AS t(ts)
ASOF LEFT JOIN (VALUES (1, 10), (4, 40)) AS p(ts, price)
ON t.ts >= p.ts
ORDER BY t.ts;
```

Expected prices: 10, 10, 40. Follow with another query on the same connection to detect invalidation.

## Secondary Mongrov findings, only when the failure points there

### Preserve core_functions in explicit extension configurations

https://github.com/mongrov/react-native-duckdb/commit/5bf4f6649b200ffb01d42dab30b48b0b8d643fb2

This makes `scripts/configure-extensions.js` always include `core_functions`. An explicit extension list omitting it can otherwise remove ordinary functions such as `sum`, `avg` and `date_trunc`. Our example already includes it. Check the generated config and effective Expo/Gradle consumer configuration before calling this the current cause. When adopting the policy, ensure source-build and prebuilt extension selection use the same normalized set.

### Cross-compilation and native dependencies

Mongrov's WIP history includes Android httpfs/Iceberg dependency and linker fixes. Treat these as reference material if the actual error concerns OpenSSL, curl, AWS SDK, roaring, package discovery or static-library resolution:

https://github.com/mongrov/react-native-duckdb/commits/59034473775aa46d1870fd1babc1eef3f9b11296/

Check app/ABI-specific vendor paths, NDK/sysroot, static archives, transitive dependencies and final link order. If a cross-compiled dependency is invisible to `find_package`, inspect its prefix/root search behavior rather than unconditionally applying global CMake overrides. Verify that all native dependencies support the app's minimum API level.

Our example does not configure Iceberg or spatial. Do not add their AWS/GDAL/PROJ/GEOS/vcpkg chains to solve an unrelated eight-extension build. Use a reduced extension configuration only as a documented diagnostic isolation step; restore and verify the intended full configuration before declaring success.

Mongrov's published prebuilts are iOS-only DuckDB 1.4.4 artifacts with different extension sets. They do not solve our Android source build or match our 1.4.5 engine. Its fork document also reports an unresolved iOS catalog issue; that is not an established Android root cause.

## Other references if the error is specifically packaging, codegen or autolinking

Shane98c adds RN Gradle-plugin/codegen declarations and a package metadata export:

- https://github.com/Shane98c/react-native-duckdb/commit/f325fc846f3b4712b7eaf9c177c2b90a80becc36
- https://github.com/Shane98c/react-native-duckdb/commit/39d5ebef195ba0203598725d3e3d5b0d6e3cb265
- https://github.com/Shane98c/react-native-duckdb/commit/121ed8719f1a384c55f0d780f63dec81cf114e70

Use these only for matching missing-plugin, missing-codegen or package-export errors, after checking our hoisted workspace layout and the target RN version. Do not wholesale merge a fork or use a prebuilt compiled against another Nitro version to mask the problem.

For future build-time optimization, Shane98c and JJK801 prebuild DuckDB static libraries while compiling the RN/Nitro wrapper in the consumer. That boundary is a better reference for the ongoing migration than CrunchyMonkies' completed wrapper binaries. Establish correctness first; do not switch distribution strategies in the middle of diagnosis without evidence that packaging is the blocker.

## Verification and completion

1. Reproduce and resolve the current failure with one ABI matching the chosen emulator/device. Inspect the installed Java/SDK/NDK/CMake tools and effective Gradle versions; do not assume a commented/default version is what runs. Avoid concurrent builds writing the same CMake/vendor outputs.
2. Use the existing Android CI command as a starting point. From `example/android`, it currently runs `./gradlew :app:assembleDebug -PreactNativeArchitectures=x86_64 --console=plain`. Choose `arm64-v8a` for an arm64 device/emulator. Add diagnostic flags such as `--stacktrace --info` when needed. Verify command availability/configuration if the Expo conversion has since changed the entry point.
3. After building, install and launch the APK. Exercise normal default-path opening, create/insert/query, close/reopen, ASOF behavior and a subsequent query. Add focused regression coverage for reproduced defects through the existing native test infrastructure.
4. Use the repository's current `scripts/run-device-tests.sh android` and `.github/workflows/ci.yml`. The script expects the app already installed and a booted target; it can start Metro if needed. It accepts `DEVICE`, `RESULT_FILE`, `MIN_PASSED`, `TIMEOUT_SECONDS` and an optional autorun query. Read its current implementation, check the expected test count/filters and avoid conflicts with an agent already using Metro/device logs. Do not equate a successful APK build with passing on-device tests.
5. Verify the intended eight-extension configuration, including an httpfs remote-file query where network is available and FTS/VSS functionality. Keep network failures distinguishable from native build/runtime failures; report any untested cases explicitly.
6. Broaden to the other supported 64-bit ABI and a release build once the first path works. Confirm no unwanted duplicate native libraries, keep `JNI_OnLoad` exported, preserve the existing size-reduction flags and avoid accidental global changes to Nitro's build mode.
7. Run applicable typecheck, lint and unit tests using the current repository scripts. Reuse existing Maestro flows where they cover changed user behavior. If a targeted cache invalidation is necessary, preserve the failing logs and explain which input/output was stale before removing it; avoid repeated blanket cleans.

Return a concise handoff: original failing task/error, demonstrated root cause, changed files and rationale, source commits adapted, exact build/test commands and results, tested ABI/API/variant, and remaining gaps. Clearly separate independently verified results from fork-author claims. Do not report the Android build as fixed based only on source inspection or a passing configure step. Do not push, publish or commit unrelated ongoing work as part of this handoff.
