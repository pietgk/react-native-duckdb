#include "HybridDuckDB.hpp"
#include <mutex>
#ifdef __ANDROID__
#include <filesystem>
#include <fstream>
#endif
#include "HybridDatabase.hpp"
#include "utils.hpp"
#include "duckdb.hpp"
#include <cstdio>
#include <stdexcept>

// Generated from the app's extension list by scripts/build-duckdb-ios.sh and
// android/CMakeLists.txt.
extern "C" int32_t duckdb_register_static_extensions(void);

namespace margelo::nitro::rnduckdb {

#ifdef __ANDROID__
// httpfs verifies HTTPS against a CA bundle, and only looks in Linux locations.
// Android keeps its roots as one file each, under names OpenSSL 3 cannot look up,
// so join them into a bundle. Done once per process to follow system updates.
static std::string writeCertificateBundle(const std::string& directory) {
  namespace fs = std::filesystem;
  for (const char* store : {"/apex/com.android.conscrypt/cacerts", "/system/etc/security/cacerts"}) {
    std::error_code error;
    fs::directory_iterator certificates(store, error);
    if (error) continue;

    auto bundlePath = directory + "/.duckdb_ca_bundle.pem";
    std::ofstream bundle(bundlePath, std::ios::binary | std::ios::trunc);
    for (const auto& certificate : certificates) {
      std::ifstream file(certificate.path(), std::ios::binary);
      bundle << file.rdbuf() << '\n';
    }
    return bundlePath;
  }
  return "";
}
#endif

static void registerLinkedExtensions() {
  static std::once_flag registered;
  std::call_once(registered, []() {
    if (duckdb_register_static_extensions() != 0) {
      throw std::runtime_error("[DuckDB] Failed to register the linked extensions");
    }
  });
}

std::string HybridDuckDB::docPath = "";
std::string HybridDuckDB::documentsDir = "";
std::string HybridDuckDB::libraryDir = "";
std::string HybridDuckDB::databaseDir = "";
std::string HybridDuckDB::externalDir = "";

std::string HybridDuckDB::getVersion() {
  return std::string(duckdb::DuckDB::LibraryVersion());
}

std::string HybridDuckDB::getDocumentsPath() { return documentsDir; }
std::string HybridDuckDB::getLibraryPath() { return libraryDir; }
std::string HybridDuckDB::getDatabasePath() { return databaseDir; }
std::string HybridDuckDB::getExternalStoragePath() { return externalDir; }
std::string HybridDuckDB::getDefaultPath() { return docPath; }

std::shared_ptr<HybridDatabaseSpec> HybridDuckDB::open(
    const std::string& path,
    const std::unordered_map<std::string, std::string>& config) {

  registerLinkedExtensions();

  duckdb::DBConfig dbConfig;

  // Mobile-safe defaults via SetOptionByName (validates against compile-time capabilities)
  dbConfig.SetOptionByName("memory_limit", duckdb::Value("256MB"));
  dbConfig.options.use_temporary_directory = true;
  dbConfig.options.temporary_directory = docPath + "/.duckdb_tmp";

  // Apply user config overrides (may include threads, memory_limit, etc.)
  for (const auto& [key, value] : config) {
    dbConfig.SetOptionByName(duckdb::Identifier(key), duckdb::Value(value));
  }

  auto resolvedPath = resolvePath(docPath, path);

  std::shared_ptr<duckdb::DuckDB> db;
  if (resolvedPath == ":memory:") {
    db = std::make_shared<duckdb::DuckDB>(nullptr, &dbConfig);
  } else {
    db = std::make_shared<duckdb::DuckDB>(resolvedPath, &dbConfig);
  }

  auto con = std::make_unique<duckdb::Connection>(*db);

  // Set home_directory at runtime so duckdb_extensions() and ATTACH relative
  // paths work on mobile (no $HOME env var on Android/iOS).
  con->Query("SET home_directory='" + docPath + "'");

#ifdef __ANDROID__
  static const std::string certificateBundle = writeCertificateBundle(docPath);
  if (!certificateBundle.empty() && config.find("ca_cert_file") == config.end()) {
    // An error here only means this build has no httpfs
    con->Query("SET GLOBAL ca_cert_file=" + duckdb::KeywordHelper::WriteQuoted(certificateBundle, '\''));
  }
#endif

  return std::make_shared<HybridDatabase>(std::move(db), std::move(con), docPath);
}

void HybridDuckDB::deleteDatabase(const std::string& path) {
  auto resolvedPath = resolvePath(docPath, path);

  if (resolvedPath == ":memory:") {
    throw std::runtime_error("[DuckDB] Cannot delete in-memory database");
  }

  std::remove(resolvedPath.c_str());
  std::remove((resolvedPath + ".wal").c_str());
  std::remove((resolvedPath + ".tmp").c_str());
}

size_t HybridDuckDB::getExternalMemorySize() noexcept {
  return 0;
}

} // namespace margelo::nitro::rnduckdb
