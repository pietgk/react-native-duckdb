# Release Process

Releases go to GitHub, not npmjs.com. One tag produces both install paths from the same tarball:

- A **GitHub Release asset**, which anyone can install from its URL without a token
- A **GitHub Packages** version, which supports version ranges but needs a token to install, even though the package is public

## How to Release

1. Update the version in `package/package.json`.

2. Commit the version bump:
   ```bash
   git commit -am "chore: bump version to X.Y.Z"
   ```

3. Create and push the tag:
   ```bash
   git tag vX.Y.Z
   git push && git push --tags
   ```

4. Check the [release](https://github.com/pietgk/react-native-duckdb/releases) and its tarball.

5. Update the version in the install URLs in `README.md`, `docs/bare-workflow.md` and `docs/expo.md`.

## First Release

Do these once:

1. Create a `release` environment with a required reviewer, under Settings > Environments. Until then the workflow runs as soon as a tag is pushed, with no approval step.

2. After the first publish, make the package public under Packages > Package settings. New packages start private, and this cannot be undone.

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
