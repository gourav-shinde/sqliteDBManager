/**
 * @file migration.cpp
 * @brief Implementation of MigrationManager and SchemaValidator
 */

#include "sqlite3db/migration.hpp"
#include "sqlite3db/transaction.hpp"
#include "sqlite3db/statement.hpp"
#include <algorithm>
#include <sstream>

namespace sqlite3db {

// ========== MigrationManager ==========

void MigrationManager::add(int version, const std::string& description,
                           std::function<void(Connection&)> up) {
    add(Migration(version, description, std::move(up)));
}

void MigrationManager::add(int version, const std::string& description,
                           std::function<void(Connection&)> up,
                           std::function<void(Connection&)> down) {
    add(Migration(version, description, std::move(up), std::move(down)));
}

void MigrationManager::add(Migration migration) {
    if (migration.version <= 0) {
        throw MigrationException("Migration version must be positive", migration.version);
    }
    if (migrations_.count(migration.version) > 0) {
        throw MigrationException("Duplicate migration version", migration.version);
    }
    migrations_.emplace(migration.version, std::move(migration));
}

void MigrationManager::ensureMigrationTable(Connection& conn) const {
    // This table tracks which migrations have been applied
    conn.execute(R"(
        CREATE TABLE IF NOT EXISTS __migrations (
            version INTEGER PRIMARY KEY,
            description TEXT NOT NULL,
            applied_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
        )
    )");
}

int MigrationManager::currentVersion(Connection& conn) const {
    ensureMigrationTable(conn);

    auto stmt = conn.prepare("SELECT MAX(version) FROM __migrations");
    if (stmt.step()) {
        if (stmt.isNull(0)) {
            return 0;
        }
        return stmt.columnInt(0);
    }
    return 0;
}

int MigrationManager::latestVersion() const {
    if (migrations_.empty()) {
        return 0;
    }
    return migrations_.rbegin()->first;
}

std::vector<Migration> MigrationManager::pending(Connection& conn) const {
    int current = currentVersion(conn);
    std::vector<Migration> result;

    for (const auto& [version, migration] : migrations_) {
        if (version > current) {
            result.push_back(migration);
        }
    }

    return result;
}

bool MigrationManager::isUpToDate(Connection& conn) const {
    return currentVersion(conn) >= latestVersion();
}

void MigrationManager::recordMigration(Connection& conn, int version) const {
    auto& migration = migrations_.at(version);
    auto stmt = conn.prepare(
        "INSERT INTO __migrations (version, description) VALUES (?, ?)");
    stmt.bind(1, version).bind(2, migration.description).execute();
}

void MigrationManager::removeMigrationRecord(Connection& conn, int version) const {
    auto stmt = conn.prepare("DELETE FROM __migrations WHERE version = ?");
    stmt.bind(1, version).execute();
}

void MigrationManager::apply(Connection& conn) {
    applyTo(conn, latestVersion());
}

void MigrationManager::applyTo(Connection& conn, int targetVersion) {
    ensureMigrationTable(conn);

    int current = currentVersion(conn);

    // Get migrations to apply
    std::vector<int> toApply;
    for (const auto& [version, _] : migrations_) {
        if (version > current && version <= targetVersion) {
            toApply.push_back(version);
        }
    }

    // Sort to ensure order
    std::sort(toApply.begin(), toApply.end());

    // Apply each migration in a transaction
    for (int version : toApply) {
        const auto& migration = migrations_.at(version);

        // Each migration runs in its own transaction
        // This allows partial progress if one fails
        Transaction txn(conn);

        try {
            migration.up(conn);
            recordMigration(conn, version);
            txn.commit();
        } catch (const std::exception& e) {
            // Transaction auto-rolls back
            throw MigrationException(
                "Migration failed: " + std::string(e.what()),
                version
            );
        }
    }
}

void MigrationManager::rollbackTo(Connection& conn, int targetVersion) {
    int current = currentVersion(conn);

    if (targetVersion >= current) {
        return;  // Nothing to rollback
    }

    // Get migrations to rollback (in reverse order)
    std::vector<int> toRollback;
    for (const auto& [version, _] : migrations_) {
        if (version > targetVersion && version <= current) {
            toRollback.push_back(version);
        }
    }

    // Sort descending for rollback
    std::sort(toRollback.begin(), toRollback.end(), std::greater<int>());

    for (int version : toRollback) {
        const auto& migration = migrations_.at(version);

        if (!migration.down) {
            throw MigrationException(
                "Migration has no rollback function",
                version
            );
        }

        Transaction txn(conn);

        try {
            migration.down(conn);
            removeMigrationRecord(conn, version);
            txn.commit();
        } catch (const std::exception& e) {
            throw MigrationException(
                "Rollback failed: " + std::string(e.what()),
                version
            );
        }
    }
}

// ========== SchemaValidator ==========

SchemaValidator& SchemaValidator::requireTable(const std::string& tableName) {
    tableRequirements_.push_back({tableName});
    return *this;
}

SchemaValidator& SchemaValidator::requireColumn(const std::string& tableName,
                                                  const std::string& columnName,
                                                  const std::string& expectedType) {
    columnRequirements_.push_back({tableName, columnName, expectedType, false});
    return *this;
}

SchemaValidator& SchemaValidator::requireNotNull(const std::string& tableName,
                                                   const std::string& columnName) {
    // Find existing column requirement or create new one
    for (auto& req : columnRequirements_) {
        if (req.tableName == tableName && req.columnName == columnName) {
            req.requireNotNull = true;
            return *this;
        }
    }
    columnRequirements_.push_back({tableName, columnName, "", true});
    return *this;
}

SchemaValidator& SchemaValidator::requireIndex(const std::string& tableName,
                                                 const std::string& indexName) {
    indexRequirements_.push_back({tableName, indexName});
    return *this;
}

std::vector<SchemaValidator::ValidationError> SchemaValidator::validate(Connection& conn) const {
    std::vector<ValidationError> errors;

    // Check tables
    for (const auto& req : tableRequirements_) {
        auto stmt = conn.prepare(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?");
        stmt.bind(1, req.name);
        if (!stmt.step()) {
            errors.push_back({
                "missing_table",
                "Required table '" + req.name + "' does not exist"
            });
        }
    }

    // Check columns
    for (const auto& req : columnRequirements_) {
        // Use PRAGMA table_info to get column information
        auto stmt = conn.prepare("PRAGMA table_info(" + req.tableName + ")");

        bool found = false;
        while (stmt.step()) {
            std::string colName = stmt.columnString(1);
            if (colName == req.columnName) {
                found = true;

                // Check type if specified
                if (!req.expectedType.empty()) {
                    std::string colType = stmt.columnString(2);
                    // SQLite types are case-insensitive
                    std::string upperExpected = req.expectedType;
                    std::string upperActual = colType;
                    for (char& c : upperExpected) c = std::toupper(c);
                    for (char& c : upperActual) c = std::toupper(c);

                    if (upperActual.find(upperExpected) == std::string::npos) {
                        errors.push_back({
                            "wrong_type",
                            "Column '" + req.tableName + "." + req.columnName +
                            "' has type '" + colType + "', expected '" + req.expectedType + "'"
                        });
                    }
                }

                // Check NOT NULL if required
                if (req.requireNotNull) {
                    int notNull = stmt.columnInt(3);
                    if (!notNull) {
                        errors.push_back({
                            "nullable",
                            "Column '" + req.tableName + "." + req.columnName +
                            "' should be NOT NULL"
                        });
                    }
                }

                break;
            }
        }

        if (!found) {
            errors.push_back({
                "missing_column",
                "Required column '" + req.tableName + "." + req.columnName +
                "' does not exist"
            });
        }
    }

    // Check indexes
    for (const auto& req : indexRequirements_) {
        auto stmt = conn.prepare(
            "SELECT 1 FROM sqlite_master WHERE type='index' AND tbl_name=? AND name=?");
        stmt.bind(1, req.tableName).bind(2, req.indexName);
        if (!stmt.step()) {
            errors.push_back({
                "missing_index",
                "Required index '" + req.indexName + "' on table '" +
                req.tableName + "' does not exist"
            });
        }
    }

    return errors;
}

void SchemaValidator::validateOrThrow(Connection& conn) const {
    auto errors = validate(conn);
    if (!errors.empty()) {
        std::ostringstream oss;
        oss << "Schema validation failed with " << errors.size() << " error(s):\n";
        for (const auto& error : errors) {
            oss << "  - " << error.message << "\n";
        }
        throw SchemaException(oss.str());
    }
}

} // namespace sqlite3db
