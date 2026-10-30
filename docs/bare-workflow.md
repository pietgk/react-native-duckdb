# Bare Workflow Setup

This guide covers setup for bare React Native projects (no Expo managed workflow). For Expo projects, see [expo.md](expo.md).

## Installation

```bash
# npm
npm install @pietgk/react-native-duckdb react-native-nitro-modules

# yarn
yarn add @pietgk/react-native-duckdb react-native-nitro-modules

# bun
bun add @pietgk/react-native-duckdb react-native-nitro-modules
```

Then install iOS pods:

```bash
cd ios && pod install
```

## Configure extensions

Extensions are configured in your app's `package.json` under the `@pietgk/react-native-duckdb` key:

```json
{
  "name": "my-app",
  "dependencies": { ... },
  "@pietgk/react-native-duckdb": {
    "build": {
      "extensions": ["core_functions", "parquet", "json"]
    }
  }
}
```

The default is **no extensions**. You must explicitly list every extension you need. Invalid names cause a build error with a clear message.

> `core_functions` is always included, even if your list leaves it out. It holds common SQL functions like `sum()` and `avg()`.

### Available extensions

| Extension | Description |
|-----------|-------------|
| `core_functions` | Essential SQL functions. Always included |
| `parquet` | Apache Parquet file format |
| `json` | JSON file format |
| `icu` | Unicode collation and locale-aware text |
| `sqlite_scanner` | Read/write SQLite databases via ATTACH |
| `httpfs` | Remote file access over HTTPS ([details](remote-data.md)) |
| `fts` | BM25 full-text search ([details](fts.md)) |
| `vss` | HNSW vector similarity search ([details](vss.md)) |
| `autocomplete` | SQL autocomplete suggestions |
| `tpch` | TPC-H benchmark data generator |
| `tpcds` | TPC-DS benchmark data generator |
| `delta` | Delta Lake table format |

## iOS build

### What happens during `pod install`

1. CocoaPods runs the `prepare_command` in `RNDuckDB.podspec`
2. The prepare command executes `scripts/build-duckdb-ios.sh`
3. The build script reads extensions from your `package.json` (via `scripts/configure-extensions.js`)
4. `configure-extensions.js` generates `duckdb/extension/extension_config_local.cmake`
5. CMake compiles DuckDB from source with selected extensions statically linked
6. The script builds for both device (`arm64`) and simulator (`arm64`), then creates a combined `DuckDB.xcframework`
7. The xcframework is vendored into the pod as a static library

The first build takes several minutes due to DuckDB compilation. Subsequent builds are cached unless the extension config changes.

### iOS-specific notes

- DuckDB is compiled with `-DCMAKE_BUILD_TYPE=Release` regardless of your app's debug/release scheme
- iOS prohibits dynamic loading (`dlopen`) -- all extensions are statically linked
- If `httpfs` is included, OpenSSL and libcurl are also compiled from source and linked into the xcframework. The podspec automatically adds `Security.framework`, `SystemConfiguration.framework`, and `libz` linker flags.

## Android build

### What happens during Gradle build

1. Gradle reads the `RNDuckDB` library's `build.gradle`, which includes the CMake external native build
2. CMake runs `scripts/configure-extensions.js` with `--app-root` pointing to your project root
3. The script reads extensions from your `package.json` and generates `extension_config_local.cmake`
4. DuckDB is compiled from source via CMake with the NDK toolchain
5. Extensions are statically linked into the final `.so` library
6. The build runs for each ABI target (typically `arm64-v8a` and `x86_64`)

DuckDB is always compiled with `-O3 -DNDEBUG` (Release optimizations) even for debug app builds, via a CMake wrapper subdirectory. Without this, debug builds would be ~30x slower.

### Android-specific notes

- Default ABI targets: `armeabi-v7a`, `x86`, `x86_64`, `arm64-v8a`. Override with `reactNativeArchitectures` in `gradle.properties`.
- If `httpfs` is enabled, 32-bit ABIs (`armeabi-v7a`, `x86`) are automatically skipped for the httpfs extension. The rest of DuckDB still builds for all ABIs.
- If `vss` is enabled, an `aligned_alloc` workaround is applied for Android API levels below 28.

## Changing extensions

After changing the `extensions` array in `package.json`, rebuild the native code.

### iOS

CocoaPods only runs the build step when it installs the pod, so remove `Pods` first. DuckDB rebuilds only if the extension list changed.

```bash
cd ios && rm -rf Pods && pod install
```

### Android

```bash
rm -rf node_modules/@pietgk/react-native-duckdb/android/.cxx
npx react-native run-android
```

## Verify extensions

After building, verify which extensions are available at runtime:

```sql
SELECT extension_name, loaded, installed FROM duckdb_extensions();
```

Extensions in your build config will show `installed = true`. Use `LOAD 'extension_name'` as a no-op to confirm availability (it succeeds silently for statically-linked extensions, throws for missing ones).

## Troubleshooting

### "DuckDB source not found"

The build downloads the DuckDB source with git, so it needs network access and git. To fetch it directly:

```bash
bash node_modules/@pietgk/react-native-duckdb/scripts/fetch-duckdb.sh
```

### iOS build fails in `fmt` on Xcode 26

React Native 0.82 pins fmt 11.0.2, which does not compile on Xcode 26. Build it as C++17 in your `Podfile`, after `react_native_post_install`:

```ruby
post_install do |installer|
  react_native_post_install(
    # ...
  )

  installer.pods_project.targets.each do |target|
    next unless target.name == 'fmt'

    target.build_configurations.each do |config|
      config.build_settings['CLANG_CXX_LANGUAGE_STANDARD'] = 'c++17'
    end
  end
end
```

### "Unknown extension" build error

Check for typos in your `package.json` extension list. Run `configure-extensions.js --help` to see valid names.

### iOS build takes too long

The first build compiles DuckDB from C++ source, which takes about 10 minutes with all extensions on a recent Mac. Builds are cached automatically. Only changing extensions triggers a recompilation.

### Android build fails with 32-bit errors

If you see httpfs-related errors on 32-bit ABIs, this is expected. Either:
- Remove `httpfs` from your extensions, or
- Limit ABIs to 64-bit in `android/gradle.properties`: `reactNativeArchitectures=arm64-v8a,x86_64`

---

*Part of [react-native-duckdb](../README.md) -- see [expo.md](expo.md) for Expo managed workflow setup.*
