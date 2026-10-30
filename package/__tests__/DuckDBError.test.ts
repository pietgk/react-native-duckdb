import { DuckDBError } from '../src/DuckDBError'

describe('DuckDBError.fromError', () => {
  it('returns an existing DuckDBError unchanged', () => {
    const error = new DuckDBError('boom')
    expect(DuckDBError.fromError(error)).toBe(error)
  })

  it('keeps the message, cause and stack of a plain Error', () => {
    const cause = new Error('root')
    const error = new Error('boom', { cause })
    const wrapped = DuckDBError.fromError(error)
    expect(wrapped).toBeInstanceOf(DuckDBError)
    expect(wrapped.message).toBe('boom')
    expect(wrapped.cause).toBe(cause)
    expect(wrapped.stack).toBe(error.stack)
  })

  it('wraps a string as the message', () => {
    expect(DuckDBError.fromError('boom').message).toBe('boom')
  })

  it('keeps unknown values as the cause', () => {
    const value = { code: 42 }
    const wrapped = DuckDBError.fromError(value)
    expect(wrapped.message).toBe('Unknown DuckDB error')
    expect(wrapped.cause).toBe(value)
  })
})
