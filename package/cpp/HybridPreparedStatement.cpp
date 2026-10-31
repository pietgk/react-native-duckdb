#include "HybridPreparedStatement.hpp"

namespace margelo::nitro::rnduckdb {

void HybridPreparedStatement::ensureNotFinalized() {
  if (_finalized) {
    throw std::runtime_error("[DuckDB] PreparedStatement is finalized");
  }
}

// Tasks hold their own reference to the statement, so finalize() can run while they execute.
static std::shared_ptr<HybridQueryResultSpec> runStatement(
    duckdb::PreparedStatement& stmt,
    const std::optional<std::vector<DuckDBValue>>& params) {
  auto values = params ? toValues(*params) : duckdb::vector<duckdb::Value>();
  auto result = stmt.Execute(values);
  if (result->HasError()) {
    throw std::runtime_error("[DuckDB] " + result->GetError());
  }
  return std::make_shared<HybridQueryResult>(std::move(result));
}

static std::shared_ptr<HybridQueryResultSpec> runStatementNamed(
    duckdb::PreparedStatement& stmt,
    const std::unordered_map<std::string, DuckDBValue>& params) {
  auto namedValues = toNamedValues(params);
  auto result = stmt.Execute(namedValues);
  if (result->HasError()) {
    throw std::runtime_error("[DuckDB] " + result->GetError());
  }
  return std::make_shared<HybridQueryResult>(std::move(result));
}

std::shared_ptr<HybridQueryResultSpec> HybridPreparedStatement::executeSync(
    const std::optional<std::vector<DuckDBValue>>& params) {
  ensureNotFinalized();
  return runStatement(*_stmt, params);
}

std::shared_ptr<Promise<std::shared_ptr<HybridQueryResultSpec>>> HybridPreparedStatement::execute(
    const std::optional<std::vector<DuckDBValue>>& params) {
  ensureNotFinalized();
  auto copiedParams = copyParamsForBackground(params);
  return Promise<std::shared_ptr<HybridQueryResultSpec>>::async(
    [stmt = _stmt, copiedParams]() -> std::shared_ptr<HybridQueryResultSpec> {
      return runStatement(*stmt, copiedParams);
    });
}

std::shared_ptr<HybridQueryResultSpec> HybridPreparedStatement::executeSyncNamed(
    const std::unordered_map<std::string, DuckDBValue>& params) {
  ensureNotFinalized();
  return runStatementNamed(*_stmt, params);
}

std::shared_ptr<Promise<std::shared_ptr<HybridQueryResultSpec>>> HybridPreparedStatement::executeNamed(
    const std::unordered_map<std::string, DuckDBValue>& params) {
  ensureNotFinalized();
  auto copiedParams = copyNamedParamsForBackground(params);
  return Promise<std::shared_ptr<HybridQueryResultSpec>>::async(
    [stmt = _stmt, copiedParams = std::move(copiedParams)]() -> std::shared_ptr<HybridQueryResultSpec> {
      return runStatementNamed(*stmt, copiedParams);
    });
}

void HybridPreparedStatement::finalize() {
  if (_finalized) return;
  _stmt.reset();
  _finalized = true;
}

size_t HybridPreparedStatement::getExternalMemorySize() noexcept {
  return _finalized ? 0 : 4096;
}

} // namespace margelo::nitro::rnduckdb
