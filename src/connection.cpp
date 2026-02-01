/**
 * @file connection.cpp
 * @brief Implementation of Connection class
 */

#include "sqlite3db/connection.hpp"
#include "sqlite3db/statement.hpp"
#include "sqlite3db/transaction.hpp"

namespace sqlite3db {

Connection::Connection(const std::string& dbPath, const ConnectionOptions& options)
    : dbPath_(dbPath)
{
    int flags = 0;

    if (options.readOnly) {
        flags = SQLITE_OPEN_READONLY;
    } else {
        flags = SQLITE_OPEN_READWRITE;
        if (options.createIfNotExists) {
            flags |= SQLITE_OPEN_CREATE;
        }
    }

    int result = sqlite3_open_v2(dbPath.c_str(), &db_, flags, nullptr);

    if (result != SQLITE_OK) {
        std::string error = db_ ? sqlite3_errmsg(db_) : "Unknown error";
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        throw ConnectionException("Failed to open database '" + dbPath + "': " + error, result);
    }

    applyOptions(options);
}

Connection::~Connection() {
    close();
}

Connection::Connection(Connection&& other) noexcept
    : db_(other.db_)
    , dbPath_(std::move(other.dbPath_))
{
    other.db_ = nullptr;
}

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        close();
        db_ = other.db_;
        dbPath_ = std::move(other.dbPath_);
        other.db_ = nullptr;
    }
    return *this;
}

std::unique_ptr<Connection> Connection::open(const std::string& dbPath,
                                              const ConnectionOptions& options) {
    return std::make_unique<Connection>(dbPath, options);
}

std::unique_ptr<Connection> Connection::inMemory(const ConnectionOptions& options) {
    return open(":memory:", options);
}

void Connection::applyOptions(const ConnectionOptions& options) {
    // Enable extended result codes for more detailed error info
    if (options.extendedResultCodes) {
        sqlite3_extended_result_codes(db_, 1);
    }

    // Set busy timeout
    sqlite3_busy_timeout(db_, options.busyTimeoutMs);

    // Enable foreign keys (off by default in SQLite!)
    if (options.enableForeignKeys) {
        execute("PRAGMA foreign_keys = ON");
    }

    // Enable WAL mode for better concurrent access
    // WAL = Write-Ahead Logging
    // Benefits:
    // - Readers don't block writers
    // - Writers don't block readers
    // - Better crash recovery
    if (options.enableWAL) {
        execute("PRAGMA journal_mode = WAL");
    }
}

void Connection::close() {
    if (db_) {
        // Finalize any remaining statements
        // Note: This is a safety measure; properly written code
        // shouldn't have dangling statements
        sqlite3_stmt* stmt = nullptr;
        while ((stmt = sqlite3_next_stmt(db_, nullptr)) != nullptr) {
            sqlite3_finalize(stmt);
        }

        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Connection::execute(const std::string& sql) {
    char* errMsg = nullptr;
    int result = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);

    if (result != SQLITE_OK) {
        std::string error = errMsg ? errMsg : "Unknown error";
        sqlite3_free(errMsg);

        // Check for constraint violations (base code is SQLITE_CONSTRAINT = 19)
        // Extended codes are (19 | (N << 8)), so (result & 0xFF) == 19
        if ((result & 0xFF) == SQLITE_CONSTRAINT) {
            throw ConstraintException(error, result);
        }
        throw QueryException(error, sql, result);
    }
}

void Connection::executeScript(const std::string& sql) {
    // sqlite3_exec handles multiple statements separated by semicolons
    execute(sql);
}

Statement Connection::prepare(const std::string& sql) {
    return Statement(*this, sql);
}

Transaction Connection::beginTransaction() {
    return Transaction(*this);
}

int64_t Connection::lastInsertRowId() const {
    return sqlite3_last_insert_rowid(db_);
}

int Connection::changes() const {
    return sqlite3_changes(db_);
}

int64_t Connection::totalChanges() const {
    return sqlite3_total_changes(db_);
}

bool Connection::tableExists(const std::string& tableName) {
    auto stmt = prepare(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?");
    stmt.bind(1, tableName);
    return stmt.step();
}

} // namespace sqlite3db
