/**
 * @file exceptions.hpp
 * @brief Custom exception hierarchy for database operations
 *
 * INDUSTRY PRACTICE #1: Custom Exception Hierarchy
 * ================================================
 * Instead of using generic exceptions, we create a hierarchy that:
 * - Provides specific error types for different failure modes
 * - Carries context (error codes, SQL statements, etc.)
 * - Allows callers to catch at the appropriate granularity
 * - Makes debugging easier with detailed error information
 *
 * This follows the principle of "fail fast, fail loud" - when something
 * goes wrong, we want maximum information about what happened.
 */

#pragma once

#include <exception>
#include <string>
#include <sqlite3.h>

namespace sqlite3db {

/**
 * @brief Base exception for all database errors
 *
 * All database-specific exceptions inherit from this, allowing
 * callers to catch all DB errors with a single catch block if desired.
 */
class DatabaseException : public std::exception {
public:
    explicit DatabaseException(std::string message, int errorCode = 0)
        : message_(std::move(message))
        , errorCode_(errorCode)
    {
        // Include error code in message for easier debugging
        if (errorCode_ != 0) {
            fullMessage_ = message_ + " (SQLite error code: " + std::to_string(errorCode_) + ")";
        } else {
            fullMessage_ = message_;
        }
    }

    const char* what() const noexcept override {
        return fullMessage_.c_str();
    }

    int errorCode() const noexcept {
        return errorCode_;
    }

    const std::string& message() const noexcept {
        return message_;
    }

protected:
    std::string message_;
    std::string fullMessage_;
    int errorCode_;
};

/**
 * @brief Thrown when database connection fails
 */
class ConnectionException : public DatabaseException {
public:
    explicit ConnectionException(const std::string& message, int errorCode = 0)
        : DatabaseException("Connection error: " + message, errorCode) {}
};

/**
 * @brief Thrown when a query fails to execute
 */
class QueryException : public DatabaseException {
public:
    QueryException(const std::string& message, const std::string& sql, int errorCode = 0)
        : DatabaseException("Query error: " + message, errorCode)
        , sql_(sql)
    {
        fullMessage_ += "\nSQL: " + sql_;
    }

    const std::string& sql() const noexcept {
        return sql_;
    }

private:
    std::string sql_;
};

/**
 * @brief Thrown when schema validation fails
 */
class SchemaException : public DatabaseException {
public:
    explicit SchemaException(const std::string& message)
        : DatabaseException("Schema error: " + message) {}
};

/**
 * @brief Thrown when a transaction operation fails
 */
class TransactionException : public DatabaseException {
public:
    explicit TransactionException(const std::string& message, int errorCode = 0)
        : DatabaseException("Transaction error: " + message, errorCode) {}
};

/**
 * @brief Thrown when constraint violation occurs (unique, foreign key, etc.)
 */
class ConstraintException : public DatabaseException {
public:
    explicit ConstraintException(const std::string& message, int errorCode = 0)
        : DatabaseException("Constraint violation: " + message, errorCode) {}
};

/**
 * @brief Thrown when migration fails
 */
class MigrationException : public DatabaseException {
public:
    MigrationException(const std::string& message, int version)
        : DatabaseException("Migration error at version " + std::to_string(version) + ": " + message)
        , version_(version) {}

    int version() const noexcept {
        return version_;
    }

private:
    int version_;
};

} // namespace sqlite3db
