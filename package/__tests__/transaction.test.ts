import { DuckDBError } from '../src/DuckDBError'
import { executeTransaction } from '../src/transaction'
import type { Database } from '../src/specs/Database.nitro'

type FakeOptions = { failOn?: string }

function createFakeDatabase({ failOn }: FakeOptions = {}) {
  const statements: string[] = []
  const finalized: string[] = []
  const conn = {
    closed: false,
    executeSync(sql: string) {
      statements.push(sql)
      if (failOn === sql) throw new Error(`failed: ${sql}`)
      return { rowsAffected: 0 }
    },
    async execute(sql: string) {
      statements.push(sql)
      if (failOn === sql) throw new Error(`failed: ${sql}`)
      return { rowsAffected: 0 }
    },
    prepare(sql: string) {
      statements.push(`prepare ${sql}`)
      return { finalize: () => finalized.push(sql) }
    },
    close() {
      conn.closed = true
    },
  }
  const db = { connect: () => conn } as unknown as Database
  return { db, conn, statements, finalized }
}

describe('executeTransaction', () => {
  it('commits and returns the callback result', async () => {
    const { db, conn, statements } = createFakeDatabase()
    const result = await executeTransaction(db, async (tx) => {
      tx.executeSync('INSERT 1')
      return 'done'
    })
    expect(result).toBe('done')
    expect(statements).toEqual(['BEGIN TRANSACTION', 'INSERT 1', 'COMMIT'])
    expect(conn.closed).toBe(true)
  })

  it('rolls back and reports the statement count when the callback throws', async () => {
    const { db, conn, statements } = createFakeDatabase()
    const error = await executeTransaction(db, async (tx) => {
      tx.executeSync('INSERT 1')
      await tx.execute('INSERT 2')
      throw new Error('boom')
    }).catch((e) => e)

    expect(error).toBeInstanceOf(DuckDBError)
    expect(error.message).toBe('boom')
    expect(error.transaction).toEqual({ statementsExecuted: 2, rolledBack: true, depth: 0 })
    expect(statements).toContain('ROLLBACK')
    expect(statements).not.toContain('COMMIT')
    expect(conn.closed).toBe(true)
  })

  it('rolls back when COMMIT fails', async () => {
    const { db, statements } = createFakeDatabase({ failOn: 'COMMIT' })
    await expect(executeTransaction(db, async () => 'done')).rejects.toThrow('failed: COMMIT')
    expect(statements).toContain('ROLLBACK')
  })

  it('finalizes prepared statements on success and on failure', async () => {
    const success = createFakeDatabase()
    await executeTransaction(success.db, async (tx) => {
      tx.prepare('SELECT 1')
    })
    expect(success.finalized).toContain('SELECT 1')

    const failure = createFakeDatabase()
    await executeTransaction(failure.db, async (tx) => {
      tx.prepare('SELECT 2')
      throw new Error('boom')
    }).catch(() => undefined)
    expect(failure.finalized).toContain('SELECT 2')
  })

  it('refuses nested transactions', async () => {
    const { db, statements } = createFakeDatabase()
    await expect(
      executeTransaction(db, async (tx) => tx.transaction(async () => 'inner'))
    ).rejects.toThrow('Nested transactions are not supported')
    expect(statements).toContain('ROLLBACK')
  })
})
