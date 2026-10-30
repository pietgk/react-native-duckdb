# Testing

| Layer | What it covers | Command | Runs in |
|---|---|---|---|
| Typecheck and lint | Types and static checks | `bun run typecheck`, `bun run lint` | Every PR |
| Jest | Logic that needs no device: extension config, config plugin, transactions, errors | `bun run test` | Every PR |
| In-app suite | The native library on a real device | `scripts/run-device-tests.sh ios` | Every PR, network tests nightly |
| Maestro flows | Screens, error cards and LogBox entries | `maestro test example/.maestro` | Nightly |
| Rebuild check | Rebuilding over the previous iOS output | `scripts/check-ios-rebuild.sh` | Nightly |
| Package install | The packed tarball in a new app outside the repo | `scripts/check-package-install.sh ios` | Nightly |

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
