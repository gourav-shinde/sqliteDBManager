/**
 * @file sqlite3db.hpp
 * @brief Main include file for sqlite3db library
 *
 * INDUSTRY PRACTICE #17: Single Include Header
 * =============================================
 * Provide a convenience header that includes everything.
 * Users can either:
 *   #include <sqlite3db/sqlite3db.hpp>  // Everything
 * Or include specific headers for faster compilation:
 *   #include <sqlite3db/connection.hpp>
 *   #include <sqlite3db/statement.hpp>
 *
 * ============================================================
 * SQLITE3DB LIBRARY - Industry Best Practices Summary
 * ============================================================
 *
 * This library demonstrates these key practices:
 *
 * 1.  RAII (Resource Acquisition Is Initialization)
 *     - Connection, Statement, Transaction all clean up automatically
 *     - No manual close/cleanup needed
 *     - Exception-safe resource management
 *
 * 2.  Prepared Statements
 *     - SQL injection prevention
 *     - Type-safe parameter binding
 *     - Performance optimization (query caching)
 *
 * 3.  Transaction Management
 *     - ACID compliance
 *     - Scoped transactions (auto-rollback on exception)
 *     - Savepoints for partial rollback
 *
 * 4.  Schema Migrations
 *     - Versioned schema changes
 *     - Automatic schema upgrades
 *     - Rollback support
 *
 * 5.  Schema Validation
 *     - Runtime schema verification
 *     - Clear error reporting
 *
 * 6.  Repository Pattern
 *     - Abstraction over data access
 *     - Clean separation of concerns
 *     - Testable business logic
 *
 * 7.  Query Builder
 *     - Fluent interface for queries
 *     - Reduces SQL string errors
 *
 * 8.  Batch Operations
 *     - Efficient bulk inserts
 *     - Configurable batch sizes
 *
 * 9.  Custom Exception Hierarchy
 *     - Specific error types
 *     - Rich error context
 *     - Catch at appropriate granularity
 *
 * 10. Non-Copyable, Moveable Resources
 *     - Prevents double-free bugs
 *     - Enables modern C++ idioms
 *
 * 11. Configuration via Options
 *     - Clean, extensible configuration
 *     - Sensible defaults
 *
 * 12. Test-Friendly Design
 *     - In-memory database support
 *     - Easy mocking/stubbing
 *
 * ============================================================
 */

#pragma once

#include "exceptions.hpp"
#include "connection.hpp"
#include "statement.hpp"
#include "transaction.hpp"
#include "migration.hpp"
#include "repository.hpp"

/**
 * @namespace sqlite3db
 * @brief Main namespace for the sqlite3db library
 *
 * All types are contained within this namespace to avoid
 * polluting the global namespace.
 */
namespace sqlite3db {

/**
 * @brief Library version information
 */
constexpr int VERSION_MAJOR = 1;
constexpr int VERSION_MINOR = 0;
constexpr int VERSION_PATCH = 0;
constexpr const char* VERSION_STRING = "1.0.0";

/**
 * @brief Get SQLite library version
 */
inline const char* sqliteVersion() {
    return sqlite3_libversion();
}

} // namespace sqlite3db
