const path = require('path')
const pak = require('../package/package.json')

module.exports = (api) => {
  api.cache(true)
  return {
    presets: ['babel-preset-expo'],
    plugins: [
      [
        'module-resolver',
        {
          extensions: ['.tsx', '.ts', '.js', '.json'],
          alias: {
            [pak.name]: path.join(__dirname, '../package', pak.source),
          },
        },
      ],
    ],
  }
}
