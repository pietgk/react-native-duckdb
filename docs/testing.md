# Testing

| Layer | What it covers | Command | Runs in |
|---|---|---|---|
| Typecheck and lint | Types and static checks | `bun run typecheck`, `bun run lint` | Every PR |
| Package build | The compiled JavaScript and config plugin | `bun run package build` | Every PR |
| Jest | Logic that needs no device: extension config, config plugin, transactions, errors | `bun run test` | Every PR |
| In-app suite | The native library on a real device | `scripts/run-device-tests.sh ios` | Every PR, network tests after merging |
| Maestro flows | Screens, error cards and LogBox entries | `maestro test example/.maestro` | After merging |
| Rebuild check | Rebuilding over the previous iOS output | `scripts/check-ios-rebuild.sh` | After merging |
| Package install | The packed tarball in a new app outside the repo | `scripts/check-package-install.sh ios` | After merging |

Pull requests that only change `docs/` or Markdown files skip the iOS and Android jobs.

## Before pushing

`mise.toml` pins the tools CI uses. After [installing mise](https://mise.jdx.dev), run `mise trust` and `mise install` once.

```bash
scripts/ci-local.sh ios
scripts/ci-local.sh android
```

Each command runs the same build as CI, installs the app and runs the in-app suite. It is much faster than waiting for CI, because this machine keeps its build output and has more cores. The first build compiles DuckDB.

- iOS uses the `iPhone 17` simulator. Set `SIMULATOR` to use another. Other booted simulators are left alone
- Android builds for this machine, which is arm64-v8a on Apple silicon. CI tests x86_64, so a local run also covers the architecture real devices use
- Stop Metro first. The script refuses to run if Metro is on port 8081, because it could be serving another checkout

## After merging

```bash
scripts/post-merge-check.sh       # checks main
scripts/post-merge-check.sh v2.1  # or another branch or tag
```

This runs the checks that are too slow for a PR on a fresh clone from GitHub, so uncommitted work and old build output cannot affect the result: a clean iOS build, the in-app suite with network tests, the Maestro flows, the rebuild check and the package install. It runs every check even if one fails, then lists the results. It deletes the clone if everything passed, and keeps it and the logs if something failed.

It uses the `iPhone 17` simulator and needs Metro to be stopped.

The [Full checks workflow](../.github/workflows/full-checks.yml) runs the same checks on a clean GitHub machine. There is no schedule, so start it by hand. [Run it before every release](../RELEASE.md).

## In-app suite

The example app registers its tests in `example/src/tests/`. Run them from the Tests tab, or without touching the UI:

```bash
scripts/run-device-tests.sh ios               # skips tests that need third-party URLs
scripts/run-device-tests.sh android network=1 # includes them
```

The app must be installed on a booted device. The script fails if a test fails or if fewer than `MIN_PASSED` tests pass, and writes `test-results.json`. It starts Metro if nothing is listening on `METRO_PORT` (default 8081). See the top of the script for the other settings.

`Benchmarks` are skipped by default. Pass `benchmarks=1` to include them.

## Maestro

```bash
maestro test --exclude-tags network example/.maestro
```

Flows tagged `network` depend on third-party URLs. Run `shared/assert-healthy.yaml` whenever a screen has loaded: it fails on any error card or LogBox entry. Run it before interacting with the screen as well as after, because a later action can clear an error.

Give any new error surface `testID="error-card"`.
