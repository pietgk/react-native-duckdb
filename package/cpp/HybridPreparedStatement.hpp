#pragma once

#include "HybridPreparedStatementSpec.hpp"
#include "HybridQueryResult.hpp"
#include "types.hpp"
#include "duckdb_includes.hpp"
#include <memory>
#include <NitroModules/Promise.hpp>

namespace margelo::nitro::rnduckdb {

class HybridPreparedStatement : public HybridPreparedStatementSpec {
public:
  explicit HybridPreparedStatement(std::shared_ptr<duckdb::PreparedStatement> stmt)
      : HybridObject(TAG), _stmt(std::move(stmt)), _finalized(false) {}

  ~HybridPreparedStatement() override {
    finalize();
  }

  std::shared_ptr<HybridQueryResultSpec> executeSync(
      const std::optional<std::vector<DuckDBValue>>& params) override;

  std::shared_ptr<Promise<std::shared_ptr<HybridQueryResultSpec>>> execute(
      const std::optional<std::vector<DuckDBValue>>& params) override;

  std::shared_ptr<HybridQueryResultSpec> executeSyncNamed(
      const std::unordered_map<std::string, DuckDBValue>& params) override;

  std::shared_ptr<Promise<std::shared_ptr<HybridQueryResultSpec>>> executeNamed(
      const std::unordered_map<std::string, DuckDBValue>& params) override;

  void finalize() override;

  size_t getExternalMemorySize() noexcept override;

private:
  void ensureNotFinalized();

  std::shared_ptr<duckdb::PreparedStatement> _stmt;
  bool _finalized;

  static constexpr auto TAG = "PreparedStatement";
};

} // namespace margelo::nitro::rnduckdb
