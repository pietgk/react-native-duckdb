import { useEffect } from 'react'
import { Linking, Platform, Settings } from 'react-native'
import { TestRunner } from './TestRunner'

const AUTORUN_URL = /^rnduckdb:\/\/autorun\b/

// Runs the suite from rnduckdb://autorun, so CI gets a result without driving
// the UI. Add ?network=0 to skip tests that depend on third-party URLs and
// ?benchmarks=1 to include benchmarks. scripts/run-device-tests.sh reads the
// summary line from the device log.
//
// iOS asks for confirmation before opening a custom URL scheme, so there the
// URL arrives as a launch argument, which Settings reads from NSUserDefaults.
export function useTestAutorun() {
  useEffect(() => {
    if (!__DEV__) return

    let running = false
    const handleUrl = async (url: string | null) => {
      if (running || !url || !AUTORUN_URL.test(url)) return
      running = true
      const results = await TestRunner.runAll({
        includeNetwork: !url.includes('network=0'),
        includeBenchmarks: url.includes('benchmarks=1'),
      })
      console.log(`[TEST SUMMARY] ${JSON.stringify(TestRunner.summarize(results))}`)
      running = false
    }

    if (Platform.OS === 'ios') {
      handleUrl(Settings.get('rnduckdbAutorun') ?? null)
      return
    }

    Linking.getInitialURL().then(handleUrl)
    const subscription = Linking.addEventListener('url', ({ url }) => handleUrl(url))
    return () => subscription.remove()
  }, [])
}
