#include "HybridDatabase.hpp"
#include "HybridPreparedStatement.hpp"
#include "HybridStreamingResult.hpp"
#include "HybridAppender.hpp"
#include "utils.hpp"
#include <NitroModules/ThreadPool.hpp>
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
  std::unique_lock<std::mutex> lock(_workMutex);
  _isOpen = false;
  interruptWork(lock, _workStarted);
  _con.reset();
  if (_isPrimary) {
    _db.reset();
  }
}

void HybridDatabase::interruptWork(std::unique_lock<std::mutex>& lock, uint64_t startedBefore) {
  // DuckDB clears the interrupt flag when a query starts, so repeat it until the
  // work has stopped. A single interrupt is lost if the query has not started.
  constexpr auto interruptInterval = std::chrono::milliseconds(5);

  while (_con && _workFinished < startedBefore) {
    _con->Interrupt();
    _workDone.wait_for(lock, interruptInterval);
  }
}

HybridDatabase::WorkScope::WorkScope(HybridDatabase& db) : _db(db) {
  std::lock_guard<std::mutex> lock(_db._workMutex);
  _db.ensureOpen();
  _db._workStarted++;
}

HybridDatabase::WorkScope::~WorkScope() {
  std::lock_guard<std::mutex> lock(_db._workMutex);
  _db._workFinished++;
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
  // Hermes counts this against its heap limit and aborts the process when the
  // total is too large, so this is a GC hint and not DuckDB's memory use, which
  // memory_limit bounds outside the JS heap. Connections share the database.
  constexpr size_t databaseSize = 8 * 1024 * 1024;
  constexpr size_t connectionSize = 64 * 1024;

  if (!_isOpen) return 0;
  return _isPrimary ? databaseSize : connectionSize;
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
    auto result = prepared->Execute(values);
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
      [self = shared_cast<HybridDatabase>(), work = std::make_shared<WorkScope>(*this), sql, copiedParams, onProgress]() mutable -> std::shared_ptr<HybridQueryResultSpec> {
        auto scope = std::move(work);
        auto result = self->executeWithProgress(sql, copiedParams, onProgress);
        return std::make_shared<HybridQueryResult>(std::move(result));
      });
  }

  return Promise<std::shared_ptr<HybridQueryResultSpec>>::async(
    [self = shared_cast<HybridDatabase>(), work = std::make_shared<WorkScope>(*this), sql, copiedParams]() mutable -> std::shared_ptr<HybridQueryResultSpec> {
      auto scope = std::move(work);
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

  uint64_t startedBefore;
  {
    std::lock_guard<std::mutex> lock(_workMutex);
    startedBefore = _workStarted;
    if (_workFinished >= startedBefore) return;
  }

  ThreadPool::shared().run([self = shared_cast<HybridDatabase>(), startedBefore]() {
    std::unique_lock<std::mutex> lock(self->_workMutex);
    self->interruptWork(lock, startedBefore);
  });
}

// --- Profiling ---

std::string HybridDatabase::getProfilingInfo() {
  ensureOpen();
  auto &config = duckdb::ClientConfig::GetConfig(*_con->context);
  if (!config.enable_profiler) {
    throw std::runtime_error(
        "[DuckDB] Profiling is not enabled. Run: PRAGMA enable_profiling = 'json'");
  }
  return _con->GetProfilingInformation(duckdb::ProfilerPrintFormat::JSON());
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

static void enableProgress(duckdb::Connection& con) {
  con.context->config.enable_progress_bar = true;
  con.context->config.print_progress_bar = false;
}

template <class Submit>
std::unique_ptr<duckdb::QueryResult> HybridDatabase::collectWithProgress(
    Submit&& submit,
    const std::optional<std::function<void(double)>>& onProgress) {

  bool wasProgressEnabled = _con->context->config.enable_progress_bar;
  if (onProgress && !wasProgressEnabled) {
    enableProgress(*_con);
  }

  auto result = submit();
  if (result->HasError()) {
    throw std::runtime_error("[DuckDB] " + result->GetError());
  }

  // Submit only starts the query. This asks for the rows to be collected.
  result->Materialize();
  runWithProgress(*result, *_con, onProgress);

  if (onProgress && !wasProgressEnabled) {
    _con->context->config.enable_progress_bar = false;
  }

  if (result->HasError()) {
    throw std::runtime_error("[DuckDB] " + result->GetError());
  }

  return result;
}

std::unique_ptr<duckdb::QueryResult> HybridDatabase::executeWithProgress(
    const std::string& sql,
    const std::optional<DuckDBParams>& params,
    const std::optional<std::function<void(double)>>& onProgress) {
  // Outlives the query: destroying a statement removes it from the connection.
  duckdb::unique_ptr<duckdb::PreparedStatement> prepared;
  return collectWithProgress([&]() -> duckdb::unique_ptr<duckdb::QueryResult> {
    if (params && !params->empty()) {
      prepared = _con->Prepare(sql);
      if (prepared->HasError()) {
        throw std::runtime_error("[DuckDB] " + prepared->GetError());
      }
      auto values = toValues(*params);
      return prepared->Submit(values);
    }
    return _con->Submit(sql);
  }, onProgress);
}

std::unique_ptr<duckdb::QueryResult> HybridDatabase::executeNamedWithProgress(
    const std::string& sql,
    const DuckDBNamedParams& params,
    const std::optional<std::function<void(double)>>& onProgress) {
  duckdb::unique_ptr<duckdb::PreparedStatement> prepared;
  return collectWithProgress([&]() -> duckdb::unique_ptr<duckdb::QueryResult> {
    prepared = _con->Prepare(sql);
    if (prepared->HasError()) {
      throw std::runtime_error("[DuckDB] " + prepared->GetError());
    }
    auto namedValues = toNamedValues(params);
    return prepared->Submit(namedValues);
  }, onProgress);
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
  auto result = prepared->Execute(namedValues);
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
      [self = shared_cast<HybridDatabase>(), work = std::make_shared<WorkScope>(*this), sql, copiedParams = std::move(copiedParams), onProgress]() mutable -> std::shared_ptr<HybridQueryResultSpec> {
        auto scope = std::move(work);
        auto result = self->executeNamedWithProgress(sql, copiedParams, onProgress);
        return std::make_shared<HybridQueryResult>(std::move(result));
      });
  }

  return Promise<std::shared_ptr<HybridQueryResultSpec>>::async(
    [self = shared_cast<HybridDatabase>(), work = std::make_shared<WorkScope>(*this), sql, copiedParams = std::move(copiedParams)]() mutable -> std::shared_ptr<HybridQueryResultSpec> {
      auto scope = std::move(work);
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

// A stream owns its connection, so close() only has to wait for its creation.
std::unique_ptr<duckdb::Connection> HybridDatabase::createStreamConnection(
    const std::optional<std::function<void(double)>>& onProgress) {
  WorkScope work(*this);
  auto con = std::make_unique<duckdb::Connection>(*_db);
  if (onProgress) {
    enableProgress(*con);
  }
  return con;
}

static std::shared_ptr<HybridStreamingResultSpec> openStream(
    std::unique_ptr<duckdb::Connection> con,
    duckdb::unique_ptr<duckdb::QueryResult> result,
    const std::optional<std::function<void(double)>>& onProgress) {
  if (result->HasError()) {
    throw std::runtime_error("[DuckDB] " + result->GetError());
  }

  auto stream = std::make_unique<duckdb::QueryResultStream<>>(std::move(result));
  runWithProgress(*stream, *con, onProgress);
  if (stream->HasError()) {
    throw std::runtime_error("[DuckDB] " + stream->GetError());
  }

  return std::make_shared<HybridStreamingResult>(std::move(stream), std::move(con));
}

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
      auto streamCon = self->createStreamConnection(onProgress);

      duckdb::unique_ptr<duckdb::PreparedStatement> prepared;
      duckdb::unique_ptr<duckdb::QueryResult> result;
      if (copiedParams && !copiedParams->empty()) {
        prepared = streamCon->Prepare(sql);
        if (prepared->HasError()) {
          throw std::runtime_error("[DuckDB] " + prepared->GetError());
        }
        auto values = toValues(*copiedParams);
        result = prepared->Submit(values);
      } else {
        result = streamCon->Submit(sql);
      }

      return openStream(std::move(streamCon), std::move(result), onProgress);
    });
}

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
      auto streamCon = self->createStreamConnection(onProgress);

      auto prepared = streamCon->Prepare(sql);
      if (prepared->HasError()) {
        throw std::runtime_error("[DuckDB] " + prepared->GetError());
      }
      auto namedValues = toNamedValues(copiedParams);
      auto result = prepared->Submit(namedValues);

      return openStream(std::move(streamCon), std::move(result), onProgress);
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
    auto addRowsAffected = [&totalRowsAffected](duckdb::QueryResult& result) {
      if (result.ColumnCount() == 1 && result.GetNames()[0].GetIdentifierName() == "Count") {
        totalRowsAffected += result.Collection().GetValue(0, 0).GetValue<int64_t>();
      }
    };

    for (const auto& cmd : commands) {
      if (cmd.params && !cmd.params->empty()) {
        auto prepared = _con->Prepare(cmd.query);
        if (prepared->HasError()) {
          throw std::runtime_error("[DuckDB] " + prepared->GetError());
        }
        auto values = toValues(*cmd.params);
        auto result = prepared->Execute(values);
        if (result->HasError()) {
          throw std::runtime_error("[DuckDB] " + result->GetError());
        }
        addRowsAffected(*result);
      } else {
        auto result = _con->Query(cmd.query);
        if (result->HasError()) {
          throw std::runtime_error("[DuckDB] " + result->GetError());
        }
        addRowsAffected(*result);
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
    [self = shared_cast<HybridDatabase>(), work = std::make_shared<WorkScope>(*this), copiedCommands = std::move(copiedCommands)]() mutable -> BatchResult {
      auto scope = std::move(work);
      return self->executeBatchSync(copiedCommands);
    });
}

} // namespace margelo::nitro::rnduckdb
