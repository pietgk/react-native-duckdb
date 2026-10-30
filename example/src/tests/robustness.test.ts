import { TestRegistry } from '../testing/TestRegistry'
import { HybridDuckDB } from 'react-native-duckdb'

const CATEGORY = 'Robustness'

function expectThrows(run: () => void, message: string) {
  try {
    run()
  } catch {
    return
  }
  throw new Error(message)
}

function countRows(db: ReturnType<typeof HybridDuckDB.open>, table: string) {
  return Number(db.executeSync(`SELECT count(*) AS cnt FROM ${table}`).toRows()[0].cnt)
}

TestRegistry.registerTest(CATEGORY, 'Appender close reports a failed flush', async () => {
  const db = HybridDuckDB.open(':memory:', {})
  try {
    db.executeSync('CREATE TABLE app_pk (id INTEGER PRIMARY KEY)')
    const appender = db.createAppender('app_pk')
    appender.appendRow([1])
    appender.appendRow([1])
    expectThrows(() => appender.close(), 'Expected close() to report the duplicate key')
    appender.close()
    if (countRows(db, 'app_pk') !== 0) throw new Error('Expected no rows after the failed flush')
  } finally {
    db.close()
  }
})

TestRegistry.registerTest(CATEGORY, 'appendRows rejects a row that is too long', async () => {
  const db = HybridDuckDB.open(':memory:', {})
  try {
    db.executeSync('CREATE TABLE app_long (id INTEGER, name VARCHAR)')
    const appender = db.createAppender('app_long')
    expectThrows(
      () => appender.appendRows([[1, 'a'], [2, 'b', 'extra']]),
      'Expected appendRows to reject the longer row'
    )
    appender.close()
    if (countRows(db, 'app_long') !== 0) throw new Error('Expected no partial append')
  } finally {
    db.close()
  }
})

TestRegistry.registerTest(CATEGORY, 'appendRows rejects a row that is too short', async () => {
  const db = HybridDuckDB.open(':memory:', {})
  try {
    db.executeSync('CREATE TABLE app_short (id INTEGER, name VARCHAR)')
    const appender = db.createAppender('app_short')
    expectThrows(
      () => appender.appendRows([[1, 'a'], [2]]),
      'Expected appendRows to reject the shorter row'
    )
    appender.close()
    if (countRows(db, 'app_short') !== 0) throw new Error('Expected no partial append')
  } finally {
    db.close()
  }
})

TestRegistry.registerTest(CATEGORY, 'attach accepts an apostrophe in the path', async () => {
  const fileName = `it's_attached_${Date.now()}.db`
  const db = HybridDuckDB.open(':memory:', {})
  try {
    db.attach(fileName, 'quoted_path')
    db.executeSync('CREATE TABLE quoted_path.t (i INTEGER)')
    db.detach('quoted_path')
  } finally {
    db.close()
    HybridDuckDB.deleteDatabase(fileName)
  }
})

TestRegistry.registerTest(CATEGORY, 'attach and detach quote the alias', async () => {
  const fileName = `alias_${Date.now()}.db`
  const alias = 'my alias; CREATE TABLE injected (i INTEGER)'
  const db = HybridDuckDB.open(':memory:', {})
  try {
    db.attach(fileName, alias)
    const databases = db.executeSync('SELECT database_name FROM duckdb_databases()').toRows()
    if (!databases.some((row) => row.database_name === alias)) {
      throw new Error('Expected the whole string to be the alias')
    }
    const injected = db.executeSync(
      "SELECT count(*) AS cnt FROM duckdb_tables() WHERE table_name = 'injected'"
    )
    if (Number(injected.toRows()[0].cnt) !== 0) throw new Error('The alias ran as SQL')
    db.detach(alias)
  } finally {
    db.close()
    HybridDuckDB.deleteDatabase(fileName)
  }
})

TestRegistry.registerTest(CATEGORY, 'A batch that throws leaves no transaction open', async () => {
  const db = HybridDuckDB.open(':memory:', {})
  try {
    db.executeSync('CREATE TABLE batch_t (i INTEGER)')
    expectThrows(
      () =>
        db.executeBatchSync([
          { query: 'INSERT INTO batch_t VALUES (1)' },
          { query: `SELECT 'not a number' AS "Count"` },
        ]),
      'Expected the batch to fail'
    )
    if (countRows(db, 'batch_t') !== 0) throw new Error('Expected the batch to roll back')
    db.executeSync('BEGIN TRANSACTION')
    db.executeSync('COMMIT')
  } finally {
    db.close()
  }
})

TestRegistry.registerTest(CATEGORY, 'A batch with params that throws leaves no transaction open', async () => {
  const db = HybridDuckDB.open(':memory:', {})
  try {
    expectThrows(
      () => db.executeBatchSync([{ query: `SELECT ? AS "Count"`, params: ['not a number'] }]),
      'Expected the batch to fail'
    )
    db.executeSync('BEGIN TRANSACTION')
    db.executeSync('COMMIT')
  } finally {
    db.close()
  }
})

// Runs for minutes unless it is interrupted.
const SLOW_QUERY = 'SELECT sum(i) AS total FROM range(500000000000) t(i)'

const QUERY_START_MS = 100

function wait(ms: number) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

function expectClosedError(run: () => void) {
  try {
    run()
  } catch (e: any) {
    if (String(e.message).includes('closed')) return
    throw e
  }
  throw new Error('Expected a closed database error')
}

TestRegistry.registerTest(CATEGORY, 'close interrupts a running query', async () => {
  const db = HybridDuckDB.open(':memory:', {})
  const query = db.execute(SLOW_QUERY)
  await wait(QUERY_START_MS)
  db.close()
  await query.then(
    () => {
      throw new Error('Expected the query to be interrupted')
    },
    () => undefined
  )
  if (db.isOpen) throw new Error('Expected the database to be closed')
  expectClosedError(() => db.executeSync('SELECT 1'))
})

TestRegistry.registerTest(CATEGORY, 'close immediately after execute does not crash', async () => {
  for (let i = 0; i < 25; i++) {
    const db = HybridDuckDB.open(':memory:', {})
    const query = db.execute('SELECT 1')
    db.close()
    await query.catch(() => undefined)
  }
})

TestRegistry.registerTest(CATEGORY, 'closeConnections interrupts a connection query', async () => {
  const db = HybridDuckDB.open(':memory:', {})
  try {
    const connection = db.connect()
    const query = connection.execute(SLOW_QUERY)
    await wait(QUERY_START_MS)
    db.closeConnections()
    await query.catch(() => undefined)
    if (connection.isOpen) throw new Error('Expected the connection to be closed')
  } finally {
    db.close()
  }
})

TestRegistry.registerTest(CATEGORY, 'finalize during a running statement does not crash', async () => {
  const db = HybridDuckDB.open(':memory:', {})
  try {
    db.executeSync('CREATE TABLE stmt_t AS SELECT i FROM range(5000000) t(i)')
    for (let i = 0; i < 25; i++) {
      const statement = db.prepare('SELECT sum(i) AS total FROM stmt_t')
      const query = statement.execute()
      statement.finalize()
      await query.catch(() => undefined)
    }
  } finally {
    db.close()
  }
})

TestRegistry.registerTest(CATEGORY, 'closing a stream during a fetch does not crash', async () => {
  const db = HybridDuckDB.open(':memory:', {})
  try {
    for (let i = 0; i < 25; i++) {
      const stream = await db.stream('SELECT i FROM range(10000000) t(i)')
      const chunk = stream.fetchChunk()
      stream.close()
      await chunk.catch(() => undefined)
    }
  } finally {
    db.close()
  }
})

TestRegistry.registerTest(CATEGORY, 'A database closed with a stream open keeps the stream safe', async () => {
  const db = HybridDuckDB.open(':memory:', {})
  const stream = await db.stream('SELECT i FROM range(10000000) t(i)')
  await stream.fetchChunk()
  db.close()
  await stream.fetchChunk().catch(() => undefined)
  stream.close()
})

TestRegistry.registerTest(CATEGORY, 'ASOF join returns the latest match', async () => {
  const db = HybridDuckDB.open(':memory:', {})
  try {
    const rows = db
      .executeSync(
        `SELECT t.ts, p.price
         FROM (VALUES (1), (3), (5)) AS t(ts)
         ASOF LEFT JOIN (VALUES (1, 10), (4, 40)) AS p(ts, price)
         ON t.ts >= p.ts
         ORDER BY t.ts`
      )
      .toRows()
    const prices = rows.map((row) => Number(row.price)).join(',')
    if (prices !== '10,10,40') throw new Error(`Expected 10,10,40, got ${prices}`)
    db.executeSync('SELECT 1')
  } finally {
    db.close()
  }
})

// Partitioned, so the join hashes its keys. DuckDB's debug assertions on those
// hashes are fatal and invalidate the whole database.
TestRegistry.registerTest(CATEGORY, 'Partitioned ASOF join leaves the database usable', async () => {
  const db = HybridDuckDB.open(':memory:', {})
  try {
    db.executeSync('CREATE TABLE trades AS SELECT i % 97 AS sym, i * 3 AS ts FROM range(200000) t(i)')
    db.executeSync('CREATE TABLE prices AS SELECT i % 97 AS sym, i * 2 AS ts, i AS price FROM range(300000) t(i)')
    const rows = db
      .executeSync(
        `SELECT count(*) AS cnt, count(p.price) AS matched
         FROM trades t ASOF LEFT JOIN prices p ON t.sym = p.sym AND t.ts >= p.ts`
      )
      .toRows()
    if (Number(rows[0].cnt) !== 200000) throw new Error(`Expected 200000 rows, got ${rows[0].cnt}`)
    if (countRows(db, 'trades') !== 200000) throw new Error('Expected the database to stay usable')
  } finally {
    db.close()
  }
})
