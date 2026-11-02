# @pietgk/react-native-duckdb

The analytical database for React Native. Run OLAP queries, full-text search and vector similarity search on iOS and Android with native C++ performance via [Nitro Modules](https://nitro.margelo.com/).

Based on [react-native-duckdb](https://github.com/pranshuchittora/react-native-duckdb) by Pranshu Chittora. This project continues that work on current React Native, Expo and DuckDB versions.

## Installation

Install the tarball attached to the latest [GitHub Release](https://github.com/pietgk/react-native-duckdb/releases), which needs no account or token. Copy the `.tgz` link from the release and run:

```bash
npm install <tarball-url> react-native-nitro-modules
```

You can also install from GitHub Packages, which needs a token even though the package is public. See [Installing from GitHub Packages](https://github.com/pietgk/react-native-duckdb/blob/main/docs/bare-workflow.md#installing-from-github-packages).

This package is not on npmjs.com. For iOS, run `pod install` after installation. Building needs git and Python 3.

## Quick start

```ts
import { HybridDuckDB } from '@pietgk/react-native-duckdb'

const db = HybridDuckDB.open(':memory:', {})

db.executeSync('CREATE TABLE users (id INTEGER, name VARCHAR, score DOUBLE)')
db.executeSync("INSERT INTO users VALUES (1, 'Alice', 95.5), (2, 'Bob', 87.3)")

const result = db.executeSync('SELECT * FROM users ORDER BY score DESC')
const rows = result.toRows()
// [{ id: 1, name: 'Alice', score: 95.5 }, { id: 2, name: 'Bob', score: 87.3 }]

db.close()
```

## Documentation

The [project README](https://github.com/pietgk/react-native-duckdb#readme) covers features, extensions, Expo and every guide. Report problems in the [issue tracker](https://github.com/pietgk/react-native-duckdb/issues).

## License

MIT
