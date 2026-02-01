/**
 * @file migration.hpp
 * @brief Database schema versioning and migration system
 *
 * INDUSTRY PRACTICE #12: Schema Migrations
 * ==========================================
 * In production systems, the database schema evolves over time:
 * - New tables are added
 * - Columns are added or modified
 * - Indexes are created for performance
 *
 * The problem: How do you update existing databases to the new schema?
 *
 * Solution: MIGRATIONS
 * - Each schema change is a numbered "migration"
 * - Database tracks which migrations have been applied
 * - On startup, run any pending migrations
 * - Migrations are idempotent and ordered
 *
 * Example workflow:
 *   Version 1: CREATE TABLE users (id, name)
 *   Version 2: ALTER TABLE users ADD COLUMN email
 *   Version 3: CREATE INDEX idx_users_email ON users(email)
 *
 * A new installation runs all migrations.
 * An existing v2 database only runs migration 3.
 *
 * INDUSTRY PRACTICE #13: Schema Validation
 * ==========================================
 * Beyond migrations, validate that the schema is correct:
 * - Required tables exist
 * - Required columns exist with correct types
 * - Indexes exist
 * This catches configuration errors and data corruption early.
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <map>
#include "connection.hpp"

namespace sqlite3db {

/**
 * @brief Represents a single migration
 *
 * Each migration has:
 * - A version number (must be unique, applied in order)
 * - A description for logging/debugging
 * - An 'up' function that applies the migration
 * - Optionally, a 'down' function for rollback (not always possible)
 */
struct Migration {
    int version;
    std::string description;
    std::function<void(Connection&)> up;
    std::function<void(Connection&)> down;  // Optional rollback

    Migration(int ver, std::string desc, std::function<void(Connection&)> upFn)
        : version(ver)
        , description(std::move(desc))
        , up(std::move(upFn))
        , down(nullptr) {}

    Migration(int ver, std::string desc,
              std::function<void(Connection&)> upFn,
              std::function<void(Connection&)> downFn)
        : version(ver)
        , description(std::move(desc))
        , up(std::move(upFn))
        , down(std::move(downFn)) {}
};

/**
 * @brief Schema migration manager
 *
 * Usage:
 *   MigrationManager migrations;
 *
 *   migrations.add(1, "Create users table", [](Connection& db) {
 *       db.execute(R"(
 *           CREATE TABLE users (
 *               id INTEGER PRIMARY KEY AUTOINCREMENT,
 *               name TEXT NOT NULL,
 *               created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
 *           )
 *       )");
 *   });
 *
 *   migrations.add(2, "Add email to users", [](Connection& db) {
 *       db.execute("ALTER TABLE users ADD COLUMN email TEXT");
 *   });
 *
 *   migrations.apply(connection);  // Runs pending migrations
 */
class MigrationManager {
public:
    MigrationManager() = default;

    /**
     * @brief Add a migration
     * @param version Version number (must be unique, > 0)
     * @param description Human-readable description
     * @param up Function to apply the migration
     */
    void add(int version, const std::string& description,
             std::function<void(Connection&)> up);

    /**
     * @brief Add a migration with rollback support
     */
    void add(int version, const std::string& description,
             std::function<void(Connection&)> up,
             std::function<void(Connection&)> down);

    /**
     * @brief Add a Migration object
     */
    void add(Migration migration);

    /**
     * @brief Apply all pending migrations
     * @param conn Database connection
     * @throws MigrationException if any migration fails
     *
     * Migrations are applied in a transaction:
     * - If a migration fails, all changes are rolled back
     * - The database version remains at the last successful migration
     */
    void apply(Connection& conn);

    /**
     * @brief Apply migrations up to a specific version
     * @param conn Database connection
     * @param targetVersion Target version number
     */
    void applyTo(Connection& conn, int targetVersion);

    /**
     * @brief Get the current schema version
     * @param conn Database connection
     * @return Current version (0 if no migrations applied)
     */
    int currentVersion(Connection& conn) const;

    /**
     * @brief Get the latest available version
     */
    int latestVersion() const;

    /**
     * @brief Get list of pending migrations
     */
    std::vector<Migration> pending(Connection& conn) const;

    /**
     * @brief Check if database is up to date
     */
    bool isUpToDate(Connection& conn) const;

    /**
     * @brief Rollback to a specific version (if down migrations exist)
     * @param conn Database connection
     * @param targetVersion Version to rollback to
     * @throws MigrationException if rollback not supported
     */
    void rollbackTo(Connection& conn, int targetVersion);

private:
    void ensureMigrationTable(Connection& conn) const;
    void recordMigration(Connection& conn, int version) const;
    void removeMigrationRecord(Connection& conn, int version) const;

    std::map<int, Migration> migrations_;
};

/**
 * @brief Schema validator for runtime checks
 *
 * INDUSTRY PRACTICE: Defensive Schema Validation
 * Even with migrations, validate the schema at startup:
 * - Catches manual DB modifications
 * - Catches corruption
 * - Provides clear error messages
 *
 * Usage:
 *   SchemaValidator validator;
 *   validator.requireTable("users")
 *            .requireColumn("users", "id", "INTEGER")
 *            .requireColumn("users", "name", "TEXT")
 *            .requireIndex("users", "idx_users_email");
 *
 *   auto errors = validator.validate(conn);
 *   if (!errors.empty()) {
 *       // Handle schema problems
 *   }
 */
class SchemaValidator {
public:
    struct ValidationError {
        std::string type;     // "missing_table", "missing_column", etc.
        std::string message;  // Human-readable description
    };

    /**
     * @brief Require a table to exist
     */
    SchemaValidator& requireTable(const std::string& tableName);

    /**
     * @brief Require a column to exist with optional type check
     * @param tableName Table containing the column
     * @param columnName Column name
     * @param expectedType Expected SQLite type (or empty for any)
     */
    SchemaValidator& requireColumn(const std::string& tableName,
                                    const std::string& columnName,
                                    const std::string& expectedType = "");

    /**
     * @brief Require a column to be NOT NULL
     */
    SchemaValidator& requireNotNull(const std::string& tableName,
                                     const std::string& columnName);

    /**
     * @brief Require an index to exist
     */
    SchemaValidator& requireIndex(const std::string& tableName,
                                   const std::string& indexName);

    /**
     * @brief Run validation
     * @param conn Database connection
     * @return List of validation errors (empty if valid)
     */
    std::vector<ValidationError> validate(Connection& conn) const;

    /**
     * @brief Validate and throw if errors found
     * @throws SchemaException with all validation errors
     */
    void validateOrThrow(Connection& conn) const;

private:
    struct TableRequirement {
        std::string name;
    };

    struct ColumnRequirement {
        std::string tableName;
        std::string columnName;
        std::string expectedType;
        bool requireNotNull = false;
    };

    struct IndexRequirement {
        std::string tableName;
        std::string indexName;
    };

    std::vector<TableRequirement> tableRequirements_;
    std::vector<ColumnRequirement> columnRequirements_;
    std::vector<IndexRequirement> indexRequirements_;
};

} // namespace sqlite3db
