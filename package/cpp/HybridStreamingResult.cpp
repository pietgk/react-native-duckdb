#include "HybridStreamingResult.hpp"
#include <NitroModules/Dispatcher.hpp>
#include <NitroModules/JSIConverter.hpp>
#include <NitroModules/ThreadPool.hpp>
#include <exception>

namespace margelo::nitro::rnduckdb {

void HybridStreamingResult::loadHybridMethods() {
  HybridStreamingResultSpec::loadHybridMethods();

  // start() needs the JS thread's dispatcher to resolve in order, and only a
  // raw method receives the runtime. See HybridAppender for how this shadows
  // the typed method.
  registerHybrids(this, [](Prototype& prototype) {
    prototype.registerRawHybridMethod("start", 0, &HybridStreamingResult::startRaw);
  });
}

HybridStreamingResult::HybridStreamingResult(
    std::unique_ptr<duckdb::QueryResultStream<>> stream,
    std::unique_ptr<duckdb::Connection> ownedConnection)
    : HybridObject(TAG),
      _stream(std::move(stream)),
      _ownedConnection(std::move(ownedConnection)) {

  for (auto& name : _stream->GetNames()) {
    _columnNames.push_back(name.GetIdentifierName());
  }
  for (auto& type : _stream->GetTypes()) {
    _columnTypes.push_back(type.ToString());
    _logicalTypes.push_back(type);
  }
}

bool HybridStreamingResult::getIsDone() {
  return _isDone;
}

double HybridStreamingResult::getColumnCount() {
  return static_cast<double>(_columnNames.size());
}

std::vector<std::string> HybridStreamingResult::getColumnNames() {
  return _columnNames;
}

std::vector<std::string> HybridStreamingResult::getColumnTypes() {
  return _columnTypes;
}

std::shared_ptr<HybridQueryResultSpec> HybridStreamingResult::fetchNextChunk() {
  std::lock_guard<std::mutex> lock(_mutex);
  if (_isDone || _closed) return nullptr;

  auto dataChunk = _stream->Fetch();
  if (_stream->HasError()) {
    throw std::runtime_error("[DuckDB] " + _stream->GetError());
  }
  if (!dataChunk || dataChunk->size() == 0) {
    _isDone = true;
    // Auto-close on exhaustion (release resources but don't lock again)
    _closed = true;
    _stream.reset();
    _ownedConnection.reset();
    return nullptr;
  }

  auto chunkSize = dataChunk->size();
  auto colCount = _logicalTypes.size();

  std::vector<std::vector<DuckDBValue>> columns(colCount);
  for (size_t c = 0; c < colCount; c++) {
    columns[c].reserve(chunkSize);
  }

  HybridQueryResult::materializeChunk(*dataChunk, _logicalTypes, columns);

  return std::make_shared<HybridQueryResult>(
    chunkSize, _columnNames, _columnTypes, std::move(columns));
}

std::shared_ptr<Promise<std::optional<std::shared_ptr<HybridQueryResultSpec>>>>
HybridStreamingResult::fetchChunk() {
  return Promise<std::optional<std::shared_ptr<HybridQueryResultSpec>>>::async(
    [self = shared_cast<HybridStreamingResult>()]() -> std::optional<std::shared_ptr<HybridQueryResultSpec>> {
      auto chunk = self->fetchNextChunk();
      if (!chunk) return std::nullopt;
      return chunk;
    });
}

void HybridStreamingResult::onChunk(
    const std::function<void(const std::shared_ptr<HybridQueryResultSpec>&)>& callback) {
  _onChunkCallback = callback;
}

// Shadowed by startRaw, see loadHybridMethods().
std::shared_ptr<Promise<void>> HybridStreamingResult::start() {
  throw std::runtime_error("[DuckDB] start() can only be called from JS");
}

jsi::Value HybridStreamingResult::startRaw(jsi::Runtime& rt, const jsi::Value& /*thisVal*/,
                                           const jsi::Value* /*args*/, size_t /*count*/) {
  if (!_onChunkCallback) {
    throw jsi::JSError(rt, "[DuckDB] Must call onChunk() before start()");
  }

  auto promise = Promise<void>::create();
  auto dispatcher = Dispatcher::getRuntimeGlobalDispatcher(rt);

  ThreadPool::shared().run(
    [self = shared_cast<HybridStreamingResult>(), callback = _onChunkCallback, promise, dispatcher]() {
      std::exception_ptr error;
      try {
        while (auto chunk = self->fetchNextChunk()) {
          callback(chunk);
        }
      } catch (...) {
        error = std::current_exception();
      }

      // Chunks reach JS through the dispatcher, so settling through it too keeps
      // the promise behind them. It also keeps the promise pending until start()
      // has returned. An already settled promise would become a resolved JS
      // promise, and await would resume before any chunk arrived.
      dispatcher->runAsync([promise, error]() {
        if (error) {
          promise->reject(error);
        } else {
          promise->resolve();
        }
      });
    });

  return JSIConverter<std::shared_ptr<Promise<void>>>::toJSI(rt, promise);
}

void HybridStreamingResult::close() {
  std::lock_guard<std::mutex> lock(_mutex);
  if (_closed) return;
  _closed = true;
  _isDone = true;
  _stream.reset();
  _ownedConnection.reset();
}

size_t HybridStreamingResult::getExternalMemorySize() noexcept {
  return _closed ? 0 : (1024 * 1024);
}

} // namespace margelo::nitro::rnduckdb
