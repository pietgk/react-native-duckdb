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
          cwd: __dirname,
          extensions: ['.tsx', '.ts', '.js', '.json'],
          // Relative, because Metro's cache is shared by every checkout on the machine.
          alias: {
            [pak.name]: path.join('../package', pak.source),
          },
        },
      ],
    ],
  }
}
