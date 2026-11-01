import { TestRegistry } from '../testing/TestRegistry'
import { HybridDuckDB } from '@pietgk/react-native-duckdb'
import type { Database } from '@pietgk/react-native-duckdb'

const CATEGORY = 'Storage Format'
const VEC = '[1.0, 2.0, 3.0]::FLOAT[3]'

function dbPath(name: string): string {
  return `${HybridDuckDB.documentsPath}/${name}.duckdb`
}

function storageVersion(db: Database, name: string): string {
  const rows = db
    .executeSync(`SELECT tags['storage_version'] AS version FROM duckdb_databases() WHERE database_name = '${name}'`)
    .toRows()
  return String(rows[0]?.version)
}

function expectVersion(db: Database, name: string, expected: string) {
  const version = storageVersion(db, name)
  if (version !== expected) throw new Error(`Expected ${name} to be ${expected}, got ${version}`)
}

function rowCount(db: Database, table: string): number {
  return Number(db.executeSync(`SELECT count(*) AS n FROM ${table}`).toRows()[0].n)
}

TestRegistry.registerTest(CATEGORY, 'a new file uses the 2.0 format by default', async () => {
  const name = 'storage_default'
  HybridDuckDB.deleteDatabase(dbPath(name))
  const db = HybridDuckDB.open(dbPath(name), {})
  try {
    expectVersion(db, name, 'v2.0.0+')
  } finally {
    db.close()
    HybridDuckDB.deleteDatabase(dbPath(name))
  }
})

TestRegistry.registerTest(CATEGORY, 'storage_compatibility_version sets the format of a new file', async () => {
  const name = 'storage_compat'
  HybridDuckDB.deleteDatabase(dbPath(name))
  const db = HybridDuckDB.open(dbPath(name), { storage_compatibility_version: 'v1.4.0' })
  try {
    expectVersion(db, name, 'v1.4.0+')
  } finally {
    db.close()
    HybridDuckDB.deleteDatabase(dbPath(name))
  }
})

TestRegistry.registerTest(CATEGORY, 'a 1.4 file keeps its format when reopened and written', async () => {
  const name = 'storage_keep'
  HybridDuckDB.deleteDatabase(dbPath(name))
  try {
    const created = HybridDuckDB.open(dbPath(name), { storage_compatibility_version: 'v1.4.0' })
    created.executeSync('CREATE TABLE t AS SELECT range AS id FROM range(100)')
    created.close()

    const reopened = HybridDuckDB.open(dbPath(name), {})
    reopened.executeSync('INSERT INTO t VALUES (100)')
    reopened.executeSync('CHECKPOINT')
    reopened.close()

    const db = HybridDuckDB.open(dbPath(name), {})
    try {
      expectVersion(db, name, 'v1.4.0+')
      if (rowCount(db, 't') !== 101) throw new Error('The write did not survive reopening')
    } finally {
      db.close()
    }
  } finally {
    HybridDuckDB.deleteDatabase(dbPath(name))
  }
})

// Real 1.4 creates v1.0.0 files unless told otherwise, so that is what an upgraded app has
TestRegistry.registerTest(CATEGORY, 'a 1.4 file upgrades to the 2.0 format with its indexes', async () => {
  const oldName = 'storage_old'
  const newName = 'storage_new'
  HybridDuckDB.deleteDatabase(dbPath(oldName))
  HybridDuckDB.deleteDatabase(dbPath(newName))
  try {
    const seed = HybridDuckDB.open(dbPath(oldName), { storage_compatibility_version: 'v1.0.0' })
    seed.executeSync("LOAD 'fts'")
    seed.executeSync("LOAD 'vss'")
    seed.executeSync('SET hnsw_enable_experimental_persistence = true')
    seed.executeSync("CREATE TABLE docs AS SELECT range AS id, 'duck number ' || range AS body FROM range(500)")
    seed.executeSync('CREATE TABLE vecs AS SELECT range AS id, [range, range * 2, range * 3]::FLOAT[3] AS v FROM range(500)')
    seed.executeSync("PRAGMA create_fts_index('docs', 'id', 'body')")
    seed.executeSync('CREATE INDEX vecs_hnsw ON vecs USING HNSW (v)')
    seed.close()

    const db = HybridDuckDB.open(dbPath(oldName), {})
    try {
      db.executeSync("LOAD 'vss'")
      db.attach(dbPath(newName), 'upgraded')
      try {
        await db.execute(`COPY FROM DATABASE ${oldName} TO upgraded`)
      } finally {
        db.detach('upgraded')
      }
      expectVersion(db, oldName, 'v1.0.0+')
    } finally {
      db.close()
    }

    const upgraded = HybridDuckDB.open(dbPath(newName), {})
    try {
      upgraded.executeSync("LOAD 'fts'")
      upgraded.executeSync("LOAD 'vss'")
      expectVersion(upgraded, newName, 'v2.0.0+')
      if (rowCount(upgraded, 'docs') !== 500) throw new Error('docs lost rows')
      if (rowCount(upgraded, 'vecs') !== 500) throw new Error('vecs lost rows')

      upgraded.executeSync("PRAGMA create_fts_index('docs', 'id', 'body', overwrite = 1)")
      const hits = upgraded
        .executeSync("SELECT * FROM (SELECT id, fts_main_docs.match_bm25(id, 'duck') AS score FROM docs) WHERE score IS NOT NULL")
        .toRows()
      if (hits.length !== 500) throw new Error(`Expected 500 search hits, got ${hits.length}`)

      const plan = upgraded
        .executeSync(`EXPLAIN SELECT id FROM vecs ORDER BY array_distance(v, ${VEC}) LIMIT 1`)
        .toRows()
      if (!JSON.stringify(plan).includes('Hnsw Index Scan')) throw new Error('The HNSW index was not used')
    } finally {
      upgraded.close()
    }
  } finally {
    HybridDuckDB.deleteDatabase(dbPath(oldName))
    HybridDuckDB.deleteDatabase(dbPath(newName))
  }
})
