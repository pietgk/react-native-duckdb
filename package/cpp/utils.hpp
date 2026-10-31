#pragma once
#include "duckdb_includes.hpp"
#include <functional>
#include <optional>
#include <string>

namespace margelo::nitro::rnduckdb {

inline std::string resolvePath(const std::string& docPath, const std::string& path) {
  if (path == ":memory:" || path.empty()) return ":memory:";
  if (path[0] == '/') return path;
  return docPath + "/" + path;
}

// Runs tasks of a submitted query on this thread until it has a result, reporting
// progress between tasks. Works on a QueryResult and on a QueryResultStream.
template <class Handle>
inline void runWithProgress(Handle& handle, duckdb::Connection& con,
                            const std::optional<std::function<void(double)>>& onProgress) {
  duckdb::QueryResultState state;
  int lastPctInt = -1;
  while (!duckdb::IsObservable(state = handle.ExecuteTask())) {
    if (state == duckdb::QueryResultState::BLOCKED) {
      handle.WaitForTask();
    }
    if (onProgress) {
      double pct = con.GetQueryProgress();
      int pctInt = static_cast<int>(pct);
      if (pct >= 0 && pctInt != lastPctInt) {
        (*onProgress)(pct);
        lastPctInt = pctInt;
      }
    }
  }
}

} // namespace margelo::nitro::rnduckdb
