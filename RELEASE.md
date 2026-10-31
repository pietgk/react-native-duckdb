# Release Process

Releases go to GitHub, not npmjs.com. One tag produces both install paths from the same tarball:

- A **GitHub Release asset**, which anyone can install from its URL without a token
- A **GitHub Packages** version, which supports version ranges but needs a token to install, even though the package is public

## How to Release

1. Update the version in `package/package.json`.

2. Merge the version bump to `main` through a PR, and wait for CI.

3. Create and push the tag. This repo also has upstream's tags if you fetch from `upstream`, so push only the new tag:
   ```bash
   git tag -a vX.Y.Z -m "vX.Y.Z"
   git push origin vX.Y.Z
   ```

4. Approve the run in [Actions](https://github.com/pietgk/react-native-duckdb/actions/workflows/release.yml).

5. Check the [release](https://github.com/pietgk/react-native-duckdb/releases), then test what was published:
   ```bash
   PACKAGE_SOURCE=https://github.com/pietgk/react-native-duckdb/releases/download/vX.Y.Z/pietgk-react-native-duckdb-X.Y.Z.tgz scripts/check-package-install.sh ios
   ```

6. Update the version in the install URLs in `README.md`, `docs/bare-workflow.md` and `docs/expo.md`.

## Environment

The `release` environment requires a reviewer and only accepts `v*` tags. Approve a run with **Review deployments**, not **Start all waiting jobs**, which bypasses the rule with admin rights.

The package is public because it is linked to this public repository. It needs no setup.

## What the Workflow Does

When a `v*` tag is pushed, the [release workflow](.github/workflows/release.yml):

- Checks that the tag matches the version in `package/package.json`
- Builds the package
- Packs it with `npm pack`
- Attests the tarball, which proves it was built here by this workflow
- Publishes that tarball to GitHub Packages
- Creates a GitHub Release with the tarball attached and generated release notes

## What Gets Published

- `src/`: TypeScript source
- `lib/`: compiled JavaScript
- `cpp/`: C++ native source
- `android/`: Android build configuration
- `ios/`: iOS platform code
- `nitrogen/`: Nitro Modules codegen output
- `plugin/`: Expo config plugin, compiled
- `app.plugin.js`: Expo plugin entry point
- `scripts/`: build scripts, including the DuckDB fetch
- `duckdb.json`: the pinned DuckDB version and commit
- `RNDuckDB.podspec` and `LICENSE`

**Not published:** DuckDB source, which is fetched when a consumer builds, build artifacts, test files and the example app.

## Verifying a Release

```bash
gh attestation verify pietgk-react-native-duckdb-X.Y.Z.tgz --repo pietgk/react-native-duckdb
```

## Publishing Safeguards

- The workflow uses the built-in `GITHUB_TOKEN`, so there is no long-lived publish token to leak
- The tag must match the package version
- The tarball is attested, and the same tarball goes to both places
- `scripts/check-package-install.sh` tests a packed tarball in a new app every night
