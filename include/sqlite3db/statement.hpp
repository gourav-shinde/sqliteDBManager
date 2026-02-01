/**
 * @file statement.hpp
 * @brief Prepared statement with type-safe parameter binding
 *
 * INDUSTRY PRACTICE #7: Prepared Statements (SQL Injection Prevention)
 * ======================================================================
 * SQL injection is one of the most common and dangerous vulnerabilities.
 *
 * NEVER do this:
 *   std::string sql = "SELECT * FROM users WHERE name = '" + userName + "'";
 *   // If userName is "'; DROP TABLE users; --", you're in trouble!
 *
 * ALWAYS do this:
 *   auto stmt = conn.prepare("SELECT * FROM users WHERE name = ?");
 *   stmt.bind(1, userName);  // Safe - value is escaped/quoted properly
 *
 * Prepared statements:
 * 1. Separate SQL structure from data - prevents injection
 * 2. Pre-compile the SQL - better performance for repeated queries
 * 3. Type-safe binding - catches type mismatches
 *
 * INDUSTRY PRACTICE #8: Fluent Interface (Method Chaining)
 * =========================================================
 * Returning *this from setters enables readable chained calls:
 *   stmt.bind(1, "John").bind(2, 25).bind(3, "john@example.com").execute();
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <variant>
#include <cstdint>
#include <sqlite3.h>
#include "exceptions.hpp"

namespace sqlite3db {

// Forward declaration
class Connection;

/**
 * @brief Represents a NULL value for binding
 *
 * SQLite has a distinct NULL type. We use this sentinel to
 * distinguish between "no value" and "NULL value".
 */
struct NullValue {};
static constexpr NullValue null{};

/**
 * @brief Variant type for SQLite values
 *
 * SQLite supports these types, and we map them to C++ types:
 * - NULL -> NullValue
 * - INTEGER -> int64_t
 * - REAL -> double
 * - TEXT -> std::string
 * - BLOB -> std::vector<uint8_t>
 */
using Value = std::variant<NullValue, int64_t, double, std::string, std::vector<uint8_t>>;

/**
 * @brief RAII wrapper for prepared statement
 *
 * Lifecycle:
 * 1. Create from SQL text (compilation happens here)
 * 2. Bind parameters
 * 3. Execute or step through results
 * 4. Reset for reuse, or let destructor clean up
 */
class Statement {
public:
    /**
     * @brief Construct a prepared statement
     * @param conn Parent connection (must outlive this statement!)
     * @param sql SQL text with ? placeholders
     * @throws QueryException if SQL is invalid
     */
    Statement(Connection& conn, const std::string& sql);

    ~Statement();

    // Non-copyable
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    // Moveable
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    // ========== Parameter Binding ==========
    // Parameters are 1-indexed (SQLite convention)

    /**
     * @brief Bind an integer value
     * @param index Parameter index (1-based)
     * @param value Integer value
     * @return Reference to self for chaining
     */
    Statement& bind(int index, int value);
    Statement& bind(int index, int64_t value);

    /**
     * @brief Bind a floating point value
     */
    Statement& bind(int index, double value);

    /**
     * @brief Bind a string value
     *
     * The string is copied, so the original can be destroyed after binding.
     */
    Statement& bind(int index, const std::string& value);
    Statement& bind(int index, const char* value);

    /**
     * @brief Bind a blob (binary data)
     */
    Statement& bind(int index, const std::vector<uint8_t>& value);

    /**
     * @brief Bind a NULL value
     */
    Statement& bind(int index, NullValue);

    /**
     * @brief Bind using variant type
     */
    Statement& bind(int index, const Value& value);

    /**
     * @brief Bind a parameter by name
     *
     * For SQL like: "INSERT INTO users (name, age) VALUES (:name, :age)"
     * Named parameters improve readability and reduce off-by-one errors.
     */
    template<typename T>
    Statement& bind(const std::string& name, const T& value) {
        int index = sqlite3_bind_parameter_index(stmt_, name.c_str());
        if (index == 0) {
            throw QueryException("Unknown parameter name: " + name, sql_);
        }
        return bind(index, value);
    }

    /**
     * @brief Clear all bindings for reuse
     */
    Statement& clearBindings();

    // ========== Execution ==========

    /**
     * @brief Execute statement that doesn't return data
     * @throws QueryException if execution fails
     *
     * Use for: INSERT, UPDATE, DELETE
     */
    void execute();

    /**
     * @brief Step to next row of results
     * @return true if a row is available, false if done
     *
     * Use for: SELECT queries
     * Pattern:
     *   while (stmt.step()) {
     *       auto name = stmt.columnString(0);
     *       auto age = stmt.columnInt(1);
     *   }
     */
    bool step();

    /**
     * @brief Reset statement for reuse with new parameters
     *
     * INDUSTRY PRACTICE: Statement Reuse
     * Reusing prepared statements is more efficient than
     * creating new ones. The SQL is already compiled.
     */
    Statement& reset();

    // ========== Column Access ==========
    // Columns are 0-indexed (SQLite convention)

    /**
     * @brief Get number of columns in result
     */
    int columnCount() const;

    /**
     * @brief Get column name
     */
    std::string columnName(int index) const;

    /**
     * @brief Get column type for current row
     */
    int columnType(int index) const;

    /**
     * @brief Check if column value is NULL
     */
    bool isNull(int index) const;

    /**
     * @brief Get column as integer
     */
    int columnInt(int index) const;
    int64_t columnInt64(int index) const;

    /**
     * @brief Get column as double
     */
    double columnDouble(int index) const;

    /**
     * @brief Get column as string
     */
    std::string columnString(int index) const;

    /**
     * @brief Get column as blob
     */
    std::vector<uint8_t> columnBlob(int index) const;

    /**
     * @brief Get column as variant (auto-detect type)
     */
    Value columnValue(int index) const;

    /**
     * @brief Get column value with optional (NULL-safe)
     *
     * INDUSTRY PRACTICE: Using std::optional for Nullable Values
     * Instead of special sentinel values (-1, empty string, etc.),
     * use std::optional to clearly represent "no value".
     */
    std::optional<int64_t> columnOptionalInt64(int index) const;
    std::optional<double> columnOptionalDouble(int index) const;
    std::optional<std::string> columnOptionalString(int index) const;

    /**
     * @brief Get the SQL text
     */
    const std::string& sql() const { return sql_; }

private:
    void checkResult(int result, const std::string& operation);
    void finalize();

    sqlite3_stmt* stmt_ = nullptr;
    Connection* conn_;
    std::string sql_;
};

} // namespace sqlite3db
