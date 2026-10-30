import React, { useEffect, useState } from 'react'
import { SafeAreaView, Text } from 'react-native'
import { HybridDuckDB } from '@pietgk/react-native-duckdb'

const EXPECTED_VERSION = 'EXPECTED_DUCKDB_VERSION'

function runSmokeTest() {
  const db = HybridDuckDB.open('smoke.duckdb', {})
  try {
    db.executeSync('CREATE OR REPLACE TABLE t AS SELECT range AS i FROM range(10)')

    // sum() comes from core_functions, which this app's extension list leaves out.
    const total = Number(db.executeSync('SELECT sum(i) AS total FROM t').toRows()[0].total)
    if (total !== 45) throw new Error(`Expected 45, got ${total}`)

    const version = String(HybridDuckDB.version)
    if (!`v${version}`.endsWith(EXPECTED_VERSION)) {
      throw new Error(`Expected DuckDB ${EXPECTED_VERSION}, got ${version}`)
    }

    const loaded = db
      .executeSync('SELECT extension_name FROM duckdb_extensions() WHERE loaded')
      .toRows()
      .map((row) => row.extension_name)
    if (!loaded.includes('json')) throw new Error(`Expected json in ${loaded}`)
    if (loaded.includes('vss')) throw new Error(`Used the repo's extension list: ${loaded}`)
  } finally {
    db.close()
  }
}

export default function App() {
  const [result, setResult] = useState('Running')

  useEffect(() => {
    try {
      runSmokeTest()
      console.log('[SMOKE PASS]')
      setResult('Passed')
    } catch (e) {
      console.log(`[SMOKE FAIL] ${e}`)
      setResult(`Failed: ${e}`)
    }
  }, [])

  return (
    <SafeAreaView>
      <Text>{result}</Text>
    </SafeAreaView>
  )
}
