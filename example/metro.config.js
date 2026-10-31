const path = require('path')
const { getDefaultConfig } = require('expo/metro-config')

const config = getDefaultConfig(__dirname)

// The DuckDB source and native build output are large and contain no JS.
const packageDir = path.resolve(__dirname, '../package')
config.resolver.blockList = [
  new RegExp(`^${packageDir}/(duckdb|vendor|android/(\\.cxx|build))/.*`),
]

module.exports = config
