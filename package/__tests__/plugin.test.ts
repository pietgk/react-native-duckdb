import type { ExpoConfig } from '@expo/config-types'
import { withAndroidExtensions } from '../plugin/src/withAndroid'
import { withIosExtensions } from '../plugin/src/withIos'

type Mods = {
  mods: {
    android: { gradleProperties: (config: unknown) => Promise<{ modResults: unknown[] }> }
    ios: {
      podfileProperties: (config: unknown) => Promise<{ modResults: Record<string, string> }>
    }
  }
}

// The with* functions mutate the config they are given.
const createConfig = (): ExpoConfig => ({ name: 'test', slug: 'test' })
const modRequest = { platform: 'android', modName: 'test', projectRoot: '/tmp' }

const property = { type: 'property', key: 'RNDuckDB_extensions', value: 'json,icu' }

async function gradleProperties(extensions: string[] | undefined, existing: unknown[] = []) {
  const config = withAndroidExtensions(createConfig(), { extensions }) as unknown as Mods
  const result = await config.mods.android.gradleProperties({
    ...createConfig(),
    modRequest,
    modResults: existing,
  })
  return result.modResults
}

async function podfileProperties(extensions: string[] | undefined) {
  const config = withIosExtensions(createConfig(), { extensions }) as unknown as Mods
  const result = await config.mods.ios.podfileProperties({
    ...createConfig(),
    modRequest,
    modResults: {},
  })
  return result.modResults
}

describe('config plugin', () => {
  it('writes the Android extension list', async () => {
    expect(await gradleProperties(['json', 'icu'])).toEqual([property])
  })

  it('replaces an earlier Android value and keeps other properties', async () => {
    const other = { type: 'property', key: 'other', value: '1' }
    const stale = { type: 'property', key: 'RNDuckDB_extensions', value: 'old' }
    expect(await gradleProperties(['json', 'icu'], [other, stale])).toEqual([other, property])
  })

  it('removes the Android value when no extensions are set', async () => {
    expect(await gradleProperties(undefined, [property])).toEqual([])
  })

  it('writes the iOS extension list', async () => {
    expect(await podfileProperties(['json', 'icu'])).toEqual({
      'react-native-duckdb.extensions': 'json,icu',
    })
  })

  it('leaves the iOS value out when no extensions are set', async () => {
    expect(await podfileProperties([])).toEqual({})
  })
})
