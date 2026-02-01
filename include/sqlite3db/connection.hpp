/**
 * @file connection.hpp
 * @brief Database connection management with RAII
 *
 * INDUSTRY PRACTICE #2: RAII (Resource Acquisition Is Initialization)
 * ====================================================================
 * RAII is THE most important C++ idiom for resource management:
 * - Resources (DB connections, file handles, memory) are tied to object lifetime
 * - Constructor acquires the resource
 * - Destructor releases the resource
 * - This guarantees cleanup even when exceptions are thrown
 *
 * INDUSTRY PRACTICE #3: Non-Copyable, Moveable Resources
 * =======================================================
 * Database connections should not be copied (would lead to double-close bugs)
 * but should be moveable (for factory functions, containers, etc.)
 *
 * INDUSTRY PRACTICE #4: Configuration via Builder/Options Pattern
 * ================================================================
 * Instead of many constructor overloads, we use an options struct
 * that can be configured fluently. This is more maintainable as
 * the number of options grows.
 */

#pragma once

#include <string>
#include <memory>
#include <functional>
#include <sqlite3.h>
#include "exceptions.hpp"

namespace sqlite3db {

/**
 * @brief Configuration options for database connection
 *
 * Using a struct with sensible defaults allows for clean configuration:
 *   ConnectionOptions opts;
 *   opts.enableWAL = true;
 *   opts.busyTimeoutMs = 5000;
 *   auto conn = Connection::open("mydb.sqlite", opts);
 */
struct ConnectionOptions {
    // Enable Write-Ahead Logging for better concurrent access
    // WAL allows readers and one writer to work simultaneously
    bool enableWAL = true;

    // Timeout when database is locked by another connection
    int busyTimeoutMs = 5000;

    // Enable foreign key enforcement (surprisingly off by default in SQLite!)
    bool enableForeignKeys = true;

    // Open in read-only mode
    bool readOnly = false;

    // Create database if it doesn't exist
    bool createIfNotExists = true;

    // Enable extended result codes for more detailed error info
    bool extendedResultCodes = true;
};

// Forward declarations
class Statement;
class Transaction;

/**
 * @brief RAII wrapper for SQLite database connection
 *
 * This class manages the lifecycle of a SQLite connection:
 * - Opens connection in constructor
 * - Closes connection in destructor
 * - Provides methods for executing SQL
 *
 * Usage:
 *   {
 *       Connection conn("mydb.sqlite");
 *       conn.execute("CREATE TABLE users (id INTEGER PRIMARY KEY)");
 *   } // Connection automatically closed here
 */
class Connection {
public:
    /**
     * @brief Open a database connection
     * @param dbPath Path to database file, or ":memory:" for in-memory DB
     * @param options Connection configuration
     * @throws ConnectionException if opening fails
     */
    explicit Connection(const std::string& dbPath,
                       const ConnectionOptions& options = ConnectionOptions{});

    // Destructor closes the connection
    ~Connection();

    // Non-copyable: prevent accidental double-close
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Moveable: allow transfer of ownership
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;

    /**
     * @brief Factory method for opening connections
     *
     * INDUSTRY PRACTICE: Factory Methods
     * Using static factory methods provides:
     * - Named construction (clearer than constructor overloads)
     * - Can return smart pointers if needed
     * - Can implement caching/pooling in the future
     */
    static std::unique_ptr<Connection> open(
        const std::string& dbPath,
        const ConnectionOptions& options = ConnectionOptions{});

    /**
     * @brief Create an in-memory database for testing
     *
     * INDUSTRY PRACTICE: Test-Friendly Design
     * In-memory databases are essential for fast, isolated unit tests.
     * Each test gets a fresh database that's automatically cleaned up.
     */
    static std::unique_ptr<Connection> inMemory(
        const ConnectionOptions& options = ConnectionOptions{});

    /**
     * @brief Execute a SQL statement without results
     * @param sql SQL statement to execute
     * @throws QueryException if execution fails
     *
     * Use for: CREATE, INSERT, UPDATE, DELETE, etc.
     */
    void execute(const std::string& sql);

    /**
     * @brief Execute multiple SQL statements separated by semicolons
     * @param sql Multiple SQL statements
     * @throws QueryException if any statement fails
     *
     * Use for: Running migration scripts, initial schema setup
     */
    void executeScript(const std::string& sql);

    /**
     * @brief Create a prepared statement
     * @param sql SQL with optional ? placeholders
     * @return Prepared statement object
     *
     * INDUSTRY PRACTICE #5: Prepared Statements
     * Always use prepared statements instead of string concatenation:
     * - Prevents SQL injection attacks
     * - Better performance for repeated queries (compiled once)
     * - Type-safe parameter binding
     */
    Statement prepare(const std::string& sql);

    /**
     * @brief Begin a new transaction
     * @return Transaction RAII guard
     *
     * INDUSTRY PRACTICE #6: Scoped Transactions
     * Returns a Transaction object that:
     * - Begins transaction on construction
     * - Rolls back on destruction if not committed
     * - Ensures transactions are always properly ended
     */
    Transaction beginTransaction();

    /**
     * @brief Get the last inserted row ID
     */
    int64_t lastInsertRowId() const;

    /**
     * @brief Get the number of rows changed by last statement
     */
    int changes() const;

    /**
     * @brief Get the total changes since connection opened
     */
    int64_t totalChanges() const;

    /**
     * @brief Check if a table exists
     * @param tableName Name of the table
     */
    bool tableExists(const std::string& tableName);

    /**
     * @brief Get the raw SQLite handle (for advanced use)
     *
     * INDUSTRY PRACTICE: Escape Hatch
     * Sometimes you need the raw handle for advanced features.
     * Exposing it allows advanced use while still providing
     * the safe abstractions for common cases.
     */
    sqlite3* handle() const { return db_; }

    /**
     * @brief Get the database path
     */
    const std::string& path() const { return dbPath_; }

    /**
     * @brief Check if connection is valid
     */
    bool isOpen() const { return db_ != nullptr; }

private:
    void applyOptions(const ConnectionOptions& options);
    void close();

    sqlite3* db_ = nullptr;
    std::string dbPath_;
};

} // namespace sqlite3db
