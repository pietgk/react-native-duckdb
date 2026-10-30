#include "HybridDatabase.hpp"
#include "HybridPreparedStatement.hpp"
#include "HybridStreamingResult.hpp"
#include "HybridAppender.hpp"
#include "utils.hpp"
#include <chrono>

namespace margelo::nitro::rnduckdb {

std::atomic<uint64_t> HybridDatabase::_connectionIdCounter{0};

bool HybridDatabase::getIsOpen() {
  return _isOpen;
}

void HybridDatabase::close(const std::optional<CloseOptions>& options) {
  if (!_isOpen) return;

  bool force = options && options->force && *options->force;

  if (_isPrimary) {
    auto openCount = _tracker->count();
    if (openCount > 0 && !force) {
      throw std::runtime_error(
        "[DuckDB] Cannot close database: " + std::to_string(openCount) +
        " connection(s) still open. Use close({ force: true }) to force.");
    }
    if (openCount > 0 && force) {
      _tracker->closeAll();
    }
  }

  if (!_isPrimary && _tracker) {
    _tracker->remove(_id);
  }

  shutdown();
}

void HybridDatabase::shutdown() {
  // DuckDB clears the interrupt flag when a query starts, so repeat it in case
  // a task was still about to begin.
  constexpr auto interruptInterval = std::chrono::milliseconds(20);

  std::unique_lock<std::mutex> lock(_workMutex);
  _isOpen = false;
  while (_activeWork > 0) {
    _con->Interrupt();
    _workDone.wait_for(lock, interruptInterval);
  }
  _con.reset();
  if (_isPrimary) {
    _db.reset();
  }
}

HybridDatabase::WorkScope::WorkScope(HybridDatabase& db) : _db(db) {
  std::lock_guard<std::mutex> lock(_db._workMutex);
  _db.ensureOpen();
  _db._activeWork++;
}

HybridDatabase::WorkScope::~WorkScope() {
  std::lock_guard<std::mutex> lock(_db._workMutex);
  _db._activeWork--;
  _db._workDone.notify_all();
}

void HybridDatabase::ensureOpen() {
  if (!_isOpen) {
    throw std::runtime_error("[DuckDB] Database is closed");
  }
}

void HybridDatabase::ensurePrimary(const char* method) {
  if (!_isPrimary) {
    throw std::runtime_error(
      std::string("[DuckDB] ") + method + " can only be called on the primary database, not on a connection");
  }
}

size_t HybridDatabase::getExternalMemorySize() noexcept {
  return _isOpen ? (256 * 1024 * 1024) : 0;
}

// --- Query execution (unchanged) ---

std::shared_ptr<HybridQueryResultSpec> HybridDatabase::executeSync(
    const std::string& sql,
    const std::optional<std::vector<DuckDBValue>>& params) {
  ensureOpen();

  if (params && !params->empty()) {
    auto prepared = _con->Prepare(sql);
    if (prepared->HasError()) {
      throw std::runtime_error("[DuckDB] " + prepared->GetError());
    }
    auto values = toValues(*params);
    auto result = prepared->Execute(values, false);
    if (result->HasError()) {
      throw std::runtime_error("[DuckDB] " + result->GetError());
    }
    return std::make_shared<HybridQueryResult>(std::move(result));
  }

  auto result = _con->Query(sql);
  if (result->HasError()) {
    throw std::runtime_error("[DuckDB] " + result->GetError());
  }
  return std::make_shared<HybridQueryResult>(std::move(result));
}

std::shared_ptr<Promise<std::shared_ptr<HybridQueryResultSpec>>> HybridDatabase::execute(
    const std::string& sql,
    const std::optional<std::vector<DuckDBValue>>& params,
    const std::optional<ExecuteOptions>& options) {
  ensureOpen();
  auto copiedParams = copyParamsForBackground(params);

  std::optional<std::function<void(double)>> onProgress;
  if (options && options->onProgress.has_value()) {
    onProgress = options->onProgress;
  } else if (_progressCallback.has_value()) {
    onProgress = _progressCallback;
  }

  if (onProgress) {
    return Promise<std::shared_ptr<HybridQueryResultSpec>>::async(
      [self = shared_cast<HybridDatabase>(), sql, copiedParams, onProgress]() -> std::shared_ptr<HybridQueryResultSpec> {
        WorkScope work(*self);
        auto result = self->executeWithProgress(sql, copiedParams, onProgress);
        return std::make_shared<HybridQueryResult>(std::move(result));
      });
  }

  return Promise<std::shared_ptr<HybridQueryResultSpec>>::async(
    [self = shared_cast<HybridDatabase>(), sql, copiedParams]() -> std::shared_ptr<HybridQueryResultSpec> {
      WorkScope work(*self);
      return self->executeSync(sql, copiedParams);
    });
}

std::shared_ptr<HybridPreparedStatementSpec> HybridDatabase::prepare(
    const std::string& sql) {
  ensureOpen();
  auto stmt = _con->Prepare(sql);
  if (stmt->HasError()) {
    throw std::runtime_error("[DuckDB] " + stmt->GetError());
  }
  return std::make_shared<HybridPreparedStatement>(std::move(stmt));
}

// --- Query cancellation ---

void HybridDatabase::cancel() {
  ensureOpen();
  _con->Interrupt();
}

// --- Profiling ---

std::string HybridDatabase::getProfilingInfo() {
  ensureOpen();
  auto &config = duckdb::ClientConfig::GetConfig(*_con->context);
  if (!config.enable_profiler) {
    throw std::runtime_error(
        "[DuckDB] Profiling is not enabled. Run: PRAGMA enable_profiling = 'json'");
  }
  return _con->GetProfilingInformation(duckdb::ProfilerPrintFormat::JSON);
}

// --- Progress callbacks ---

void HybridDatabase::setProgressCallback(const std::function<void(double)>& callback) {
  ensureOpen();
  _progressCallback = callback;
  _con->context->config.enable_progress_bar = true;
  _con->context->config.print_progress_bar = false;
}

void HybridDatabase::removeProgressCallback() {
  ensureOpen();
  _progressCallback = std::nullopt;
  _con->context->config.enable_progress_bar = false;
}

// --- Progress-aware execution helpers ---

std::unique_ptr<duckdb::QueryResult> HybridDatabase::executeWithProgress(
    const std::string& sql,
    const std::optional<DuckDBParams>& params,
    const std::optional<std::function<void(double)>>& onProgress) {

  bool wasProgressEnabled = _con->context->config.enable_progress_bar;
  if (onProgress && !wasProgressEnabled) {
    _con->context->config.enable_progress_bar = true;
    _con->context->config.print_progress_bar = false;
  }

  std::unique_ptr<duckdb::PendingQueryResult> pending;
  if (params && !params->empty()) {
    auto prepared = _con->Prepare(sql);
    if (prepared->HasError()) {
      throw std::runtime_error("[DuckDB] " + prepared->GetError());
    }
    auto values = toValues(*params);
    pending = prepared->PendingQuery(values, false);
  } else {
    pending = _con->PendingQuery(sql, false);
  }

  if (pending->HasError()) {
    throw std::runtime_error("[DuckDB] " + pending->GetError());
  }

  duckdb::PendingExecutionResult status;
  int lastPctInt = -1;
  while (!duckdb::PendingQueryResult::IsResultReady(
             status = pending->ExecuteTask())) {
    if (status == duckdb::PendingExecutionResult::EXECUTION_ERROR) {
      break;
    }
    if (status == duckdb::PendingExecutionResult::BLOCKED) {
      pending->WaitForTask();
    }
    if (onProgress) {
      double pct = _con->GetQueryProgress();
      int pctInt = static_cast<int>(pct);
      if (pct >= 0 && pctInt != lastPctInt) {
        (*onProgress)(pct);
        lastPctInt = pctInt;
      }
    }
  }

  auto result = pending->Execute();

  if (onProgress && !wasProgressEnabled) {
    _con->context->config.enable_progress_bar = false;
  }

  if (result->HasError()) {
    throw std::runtime_error("[DuckDB] " + result->GetError());
  }

  return result;
}

std::unique_ptr<duckdb::QueryResult> HybridDatabase::executeNamedWithProgress(
    const std::string& sql,
    const DuckDBNamedParams& params,
    const std::optional<std::function<void(double)>>& onProgress) {

  bool wasProgressEnabled = _con->context->config.enable_progress_bar;
  if (onProgress && !wasProgressEnabled) {
    _con->context->config.enable_progress_bar = true;
    _con->context->config.print_progress_bar = false;
  }

  auto prepared = _con->Prepare(sql);
  if (prepared->HasError()) {
    throw std::runtime_error("[DuckDB] " + prepared->GetError());
  }
  auto namedValues = toNamedValues(params);
  auto pending = prepared->PendingQuery(namedValues, false);

  if (pending->HasError()) {
    throw std::runtime_error("[DuckDB] " + pending->GetError());
  }

  duckdb::PendingExecutionResult status;
  int lastPctInt = -1;
  while (!duckdb::PendingQueryResult::IsResultReady(
             status = pending->ExecuteTask())) {
    if (status == duckdb::PendingExecutionResult::EXECUTION_ERROR) {
      break;
    }
    if (status == duckdb::PendingExecutionResult::BLOCKED) {
      pending->WaitForTask();
    }
    if (onProgress) {
      double pct = _con->GetQueryProgress();
      int pctInt = static_cast<int>(pct);
      if (pct >= 0 && pctInt != lastPctInt) {
        (*onProgress)(pct);
        lastPctInt = pctInt;
      }
    }
  }

  auto result = pending->Execute();

  if (onProgress && !wasProgressEnabled) {
    _con->context->config.enable_progress_bar = false;
  }

  if (result->HasError()) {
    throw std::runtime_error("[DuckDB] " + result->GetError());
  }

  return result;
}

// --- Named parameter execution ---

std::shared_ptr<HybridQueryResultSpec> HybridDatabase::executeSyncNamed(
    const std::string& sql,
    const std::unordered_map<std::string, DuckDBValue>& params) {
  ensureOpen();
  auto prepared = _con->Prepare(sql);
  if (prepared->HasError()) {
    throw std::runtime_error("[DuckDB] " + prepared->GetError());
  }
  auto namedValues = toNamedValues(params);
  auto result = prepared->Execute(namedValues, false);
  if (result->HasError()) {
    throw std::runtime_error("[DuckDB] " + result->GetError());
  }
  return std::make_shared<HybridQueryResult>(std::move(result));
}

std::shared_ptr<Promise<std::shared_ptr<HybridQueryResultSpec>>> HybridDatabase::executeNamed(
    const std::string& sql,
    const std::unordered_map<std::string, DuckDBValue>& params,
    const std::optional<ExecuteOptions>& options) {
  ensureOpen();
  auto copiedParams = copyNamedParamsForBackground(params);

  std::optional<std::function<void(double)>> onProgress;
  if (options && options->onProgress.has_value()) {
    onProgress = options->onProgress;
  } else if (_progressCallback.has_value()) {
    onProgress = _progressCallback;
  }

  if (onProgress) {
    return Promise<std::shared_ptr<HybridQueryResultSpec>>::async(
      [self = shared_cast<HybridDatabase>(), sql, copiedParams = std::move(copiedParams), onProgress]() -> std::shared_ptr<HybridQueryResultSpec> {
        WorkScope work(*self);
        auto result = self->executeNamedWithProgress(sql, copiedParams, onProgress);
        return std::make_shared<HybridQueryResult>(std::move(result));
      });
  }

  return Promise<std::shared_ptr<HybridQueryResultSpec>>::async(
    [self = shared_cast<HybridDatabase>(), sql, copiedParams = std::move(copiedParams)]() -> std::shared_ptr<HybridQueryResultSpec> {
      WorkScope work(*self);
      return self->executeSyncNamed(sql, copiedParams);
    });
}

// --- Connection management ---

std::shared_ptr<HybridDatabaseSpec> HybridDatabase::connect() {
  ensureOpen();
  ensurePrimary("connect()");

  auto id = "conn_" + std::to_string(_connectionIdCounter.fetch_add(1));
  auto con = std::make_unique<duckdb::Connection>(*_db);

  auto child = std::make_shared<HybridDatabase>(_db, std::move(con), id, _tracker, _docPath);
  _tracker->add(id, child);

  return child;
}

ConnectionInfo HybridDatabase::connections() {
  ensureOpen();
  ensurePrimary("connections()");

  auto ids = _tracker->list();
  return ConnectionInfo(static_cast<double>(ids.size()), std::move(ids));
}

void HybridDatabase::closeConnections() {
  ensureOpen();
  ensurePrimary("closeConnections()");

  _tracker->closeAll();
}

// ConnectionTracker::closeAll — closes all tracked connections
void ConnectionTracker::closeAll() {
  // Collect pointers under lock, then close outside lock
  // (close() calls tracker->remove() which also locks)
  std::vector<std::shared_ptr<HybridDatabase>> toClose;
  {
    std::lock_guard<std::mutex> lk(mtx);
    toClose.reserve(connections.size());
    for (const auto& [_, weakConn] : connections) {
      if (auto conn = weakConn.lock()) {
        toClose.push_back(std::move(conn));
      }
    }
  }
  for (const auto& conn : toClose) {
    conn->close(std::nullopt);
  }
}

// --- Streaming ---

std::shared_ptr<Promise<std::shared_ptr<HybridStreamingResultSpec>>> HybridDatabase::stream(
    const std::string& sql,
    const std::optional<std::vector<DuckDBValue>>& params,
    const std::optional<ExecuteOptions>& options) {
  ensureOpen();
  auto copiedParams = copyParamsForBackground(params);

  std::optional<std::function<void(double)>> onProgress;
  if (options && options->onProgress.has_value()) {
    onProgress = options->onProgress;
  } else if (_progressCallback.has_value()) {
    onProgress = _progressCallback;
  }

  return Promise<std::shared_ptr<HybridStreamingResultSpec>>::async(
    [self = shared_cast<HybridDatabase>(), sql, copiedParams, onProgress]() -> std::shared_ptr<HybridStreamingResultSpec> {
      // The stream owns this connection, so close() only has to wait for its creation.
      auto streamCon = [&self] {
        WorkScope work(*self);
        return std::make_unique<duckdb::Connection>(*self->_db);
      }();

      if (onProgress) {
        streamCon->context->config.enable_progress_bar = true;
        streamCon->context->config.print_progress_bar = false;

        std::unique_ptr<duckdb::PendingQueryResult> pending;
        if (copiedParams && !copiedParams->empty()) {
          auto prepared = streamCon->Prepare(sql);
          if (prepared->HasError()) {
            throw std::runtime_error("[DuckDB] " + prepared->GetError());
          }
          auto values = toValues(*copiedParams);
          pending = prepared->PendingQuery(values, true);
        } else {
          pending = streamCon->PendingQuery(sql, true);
        }

        if (pending->HasError()) {
          throw std::runtime_error("[DuckDB] " + pending->GetError());
        }

        duckdb::PendingExecutionResult status;
        int lastPctInt = -1;
        while (!duckdb::PendingQueryResult::IsResultReady(
                   status = pending->ExecuteTask())) {
          if (status == duckdb::PendingExecutionResult::EXECUTION_ERROR) {
            break;
          }
          if (status == duckdb::PendingExecutionResult::BLOCKED) {
            pending->WaitForTask();
          }
          double pct = streamCon->GetQueryProgress();
          int pctInt = static_cast<int>(pct);
          if (pct >= 0 && pctInt != lastPctInt) {
            (*onProgress)(pct);
            lastPctInt = pctInt;
          }
        }

        auto result = pending->Execute();
        if (result->HasError()) {
          throw std::runtime_error("[DuckDB] " + result->GetError());
        }

        return std::make_shared<HybridStreamingResult>(
          std::move(result), std::move(streamCon));
      }

      std::unique_ptr<duckdb::QueryResult> result;
      if (copiedParams && !copiedParams->empty()) {
        auto prepared = streamCon->Prepare(sql);
        if (prepared->HasError()) {
          throw std::runtime_error("[DuckDB] " + prepared->GetError());
        }
        auto values = toValues(*copiedParams);
        result = prepared->Execute(values, true);
      } else {
        result = streamCon->SendQuery(sql);
      }

      if (result->HasError()) {
        throw std::runtime_error("[DuckDB] " + result->GetError());
      }

      return std::make_shared<HybridStreamingResult>(
        std::move(result), std::move(streamCon));
    });
}

// --- Streaming (named params) ---

std::shared_ptr<Promise<std::shared_ptr<HybridStreamingResultSpec>>> HybridDatabase::streamNamed(
    const std::string& sql,
    const std::unordered_map<std::string, DuckDBValue>& params,
    const std::optional<ExecuteOptions>& options) {
  ensureOpen();
  auto copiedParams = copyNamedParamsForBackground(params);

  std::optional<std::function<void(double)>> onProgress;
  if (options && options->onProgress.has_value()) {
    onProgress = options->onProgress;
  } else if (_progressCallback.has_value()) {
    onProgress = _progressCallback;
  }

  return Promise<std::shared_ptr<HybridStreamingResultSpec>>::async(
    [self = shared_cast<HybridDatabase>(), sql, copiedParams = std::move(copiedParams), onProgress]() -> std::shared_ptr<HybridStreamingResultSpec> {
      // The stream owns this connection, so close() only has to wait for its creation.
      auto streamCon = [&self] {
        WorkScope work(*self);
        return std::make_unique<duckdb::Connection>(*self->_db);
      }();

      if (onProgress) {
        streamCon->context->config.enable_progress_bar = true;
        streamCon->context->config.print_progress_bar = false;

        auto prepared = streamCon->Prepare(sql);
        if (prepared->HasError()) {
          throw std::runtime_error("[DuckDB] " + prepared->GetError());
        }
        auto namedValues = toNamedValues(copiedParams);
        auto pending = prepared->PendingQuery(namedValues, true);

        if (pending->HasError()) {
          throw std::runtime_error("[DuckDB] " + pending->GetError());
        }

        duckdb::PendingExecutionResult status;
        int lastPctInt = -1;
        while (!duckdb::PendingQueryResult::IsResultReady(
                   status = pending->ExecuteTask())) {
          if (status == duckdb::PendingExecutionResult::EXECUTION_ERROR) {
            break;
          }
          if (status == duckdb::PendingExecutionResult::BLOCKED) {
            pending->WaitForTask();
          }
          double pct = streamCon->GetQueryProgress();
          int pctInt = static_cast<int>(pct);
          if (pct >= 0 && pctInt != lastPctInt) {
            (*onProgress)(pct);
            lastPctInt = pctInt;
          }
        }

        auto result = pending->Execute();
        if (result->HasError()) {
          throw std::runtime_error("[DuckDB] " + result->GetError());
        }

        return std::make_shared<HybridStreamingResult>(
          std::move(result), std::move(streamCon));
      }

      auto prepared = streamCon->Prepare(sql);
      if (prepared->HasError()) {
        throw std::runtime_error("[DuckDB] " + prepared->GetError());
      }
      auto namedValues = toNamedValues(copiedParams);
      auto result = prepared->Execute(namedValues, true);

      if (result->HasError()) {
        throw std::runtime_error("[DuckDB] " + result->GetError());
      }

      return std::make_shared<HybridStreamingResult>(
        std::move(result), std::move(streamCon));
    });
}

// --- Appender ---

std::shared_ptr<HybridAppenderSpec> HybridDatabase::createAppender(
    const std::string& table,
    const std::optional<AppenderOptions>& options) {
  ensureOpen();
  std::optional<double> flushEvery;
  if (options && options->flushEvery) {
    flushEvery = *options->flushEvery;
  }
  return std::make_shared<HybridAppender>(*_con, table, flushEvery);
}

// --- ATTACH/DETACH ---

void HybridDatabase::attach(const std::string& path, const std::string& alias,
                             const std::optional<AttachOptions>& options) {
  ensureOpen();

  auto resolvedPath = resolvePath(_docPath, path);

  std::string sql = "ATTACH " + duckdb::KeywordHelper::WriteQuoted(resolvedPath, '\'') + " AS " +
                    duckdb::KeywordHelper::WriteOptionallyQuoted(alias);

  // Build options list
  std::vector<std::string> opts;
  if (options) {
    if (options->readOnly && *options->readOnly) {
      opts.push_back("READ_ONLY");
    }
    if (options->type && !options->type->empty()) {
      opts.push_back("TYPE " + duckdb::KeywordHelper::WriteOptionallyQuoted(*options->type));
    }
  }

  if (!opts.empty()) {
    sql += " (";
    for (size_t i = 0; i < opts.size(); i++) {
      if (i > 0) sql += ", ";
      sql += opts[i];
    }
    sql += ")";
  }

  auto result = _con->Query(sql);
  if (result->HasError()) {
    throw std::runtime_error("[DuckDB] " + result->GetError());
  }
}

void HybridDatabase::detach(const std::string& alias) {
  ensureOpen();

  auto result = _con->Query("DETACH " + duckdb::KeywordHelper::WriteOptionallyQuoted(alias));
  if (result->HasError()) {
    throw std::runtime_error("[DuckDB] " + result->GetError());
  }
}

// --- Batch execution ---

BatchResult HybridDatabase::executeBatchSync(const std::vector<BatchCommand>& commands) {
  ensureOpen();

  auto beginResult = _con->Query("BEGIN TRANSACTION");
  if (beginResult->HasError()) {
    throw std::runtime_error("[DuckDB] " + beginResult->GetError());
  }

  // Anything below can throw, including reading a "Count" column that is not a number.
  try {
    double totalRowsAffected = 0;

    for (const auto& cmd : commands) {
      if (cmd.params && !cmd.params->empty()) {
        auto prepared = _con->Prepare(cmd.query);
        if (prepared->HasError()) {
          throw std::runtime_error("[DuckDB] " + prepared->GetError());
        }
        auto values = toValues(*cmd.params);
        auto queryResult = prepared->Execute(values, false);
        if (queryResult->HasError()) {
          throw std::runtime_error("[DuckDB] " + queryResult->GetError());
        }
        auto& result = queryResult->Cast<duckdb::MaterializedQueryResult>();
        if (result.ColumnCount() == 1 && result.names[0] == "Count") {
          totalRowsAffected += result.GetValue(0, 0).GetValue<int64_t>();
        }
      } else {
        auto result = _con->Query(cmd.query);
        if (result->HasError()) {
          throw std::runtime_error("[DuckDB] " + result->GetError());
        }
        if (result->ColumnCount() == 1 && result->names[0] == "Count") {
          totalRowsAffected += result->GetValue(0, 0).GetValue<int64_t>();
        }
      }
    }

    auto commitResult = _con->Query("COMMIT");
    if (commitResult->HasError()) {
      throw std::runtime_error("[DuckDB] " + commitResult->GetError());
    }

    return BatchResult(totalRowsAffected);
  } catch (...) {
    _con->Query("ROLLBACK");
    throw;
  }
}

std::shared_ptr<Promise<BatchResult>> HybridDatabase::executeBatch(
    const std::vector<BatchCommand>& commands) {
  ensureOpen();

  // Deep-copy commands for background thread safety
  std::vector<BatchCommand> copiedCommands;
  copiedCommands.reserve(commands.size());
  for (const auto& cmd : commands) {
    auto copiedParams = copyParamsForBackground(cmd.params);
    copiedCommands.push_back(BatchCommand(cmd.query, std::move(copiedParams)));
  }

  return Promise<BatchResult>::async(
    [self = shared_cast<HybridDatabase>(), copiedCommands = std::move(copiedCommands)]() -> BatchResult {
      WorkScope work(*self);
      return self->executeBatchSync(copiedCommands);
    });
}

} // namespace margelo::nitro::rnduckdb
