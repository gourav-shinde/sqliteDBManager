/**
 * @file transaction.cpp
 * @brief Implementation of Transaction and Savepoint classes
 */

#include "sqlite3db/transaction.hpp"
#include "sqlite3db/connection.hpp"

namespace sqlite3db {

// ========== Transaction ==========

Transaction::Transaction(Connection& conn, TransactionType type)
    : conn_(&conn)
{
    std::string sql;
    switch (type) {
        case TransactionType::Deferred:
            sql = "BEGIN DEFERRED TRANSACTION";
            break;
        case TransactionType::Immediate:
            sql = "BEGIN IMMEDIATE TRANSACTION";
            break;
        case TransactionType::Exclusive:
            sql = "BEGIN EXCLUSIVE TRANSACTION";
            break;
    }

    try {
        conn_->execute(sql);
    } catch (const QueryException& e) {
        throw TransactionException("Failed to begin transaction: " + std::string(e.what()), e.errorCode());
    }
}

Transaction::~Transaction() {
    // IMPORTANT: Destructor should not throw
    // If still active (not committed/rolled back), rollback
    if (active_ && conn_) {
        try {
            conn_->execute("ROLLBACK");
        } catch (...) {
            // Log error but don't throw from destructor
            // In production, you'd use a logging framework here
        }
    }
}

Transaction::Transaction(Transaction&& other) noexcept
    : conn_(other.conn_)
    , active_(other.active_)
{
    other.conn_ = nullptr;
    other.active_ = false;
}

Transaction& Transaction::operator=(Transaction&& other) noexcept {
    if (this != &other) {
        // Rollback current transaction if active
        if (active_ && conn_) {
            try {
                conn_->execute("ROLLBACK");
            } catch (...) {
                // Ignore errors in move assignment
            }
        }

        conn_ = other.conn_;
        active_ = other.active_;
        other.conn_ = nullptr;
        other.active_ = false;
    }
    return *this;
}

void Transaction::commit() {
    if (!active_) {
        throw TransactionException("Transaction already ended");
    }

    try {
        conn_->execute("COMMIT");
        active_ = false;
    } catch (const QueryException& e) {
        throw TransactionException("Failed to commit: " + std::string(e.what()), e.errorCode());
    }
}

void Transaction::rollback() {
    if (!active_) {
        throw TransactionException("Transaction already ended");
    }

    try {
        conn_->execute("ROLLBACK");
        active_ = false;
    } catch (const QueryException& e) {
        throw TransactionException("Failed to rollback: " + std::string(e.what()), e.errorCode());
    }
}

Transaction::Savepoint Transaction::savepoint(const std::string& name) {
    if (!active_) {
        throw TransactionException("Cannot create savepoint: transaction not active");
    }
    return Savepoint(*conn_, name);
}

// ========== Savepoint ==========

Transaction::Savepoint::Savepoint(Connection& conn, const std::string& name)
    : conn_(&conn)
    , name_(name)
{
    try {
        conn_->execute("SAVEPOINT " + name_);
    } catch (const QueryException& e) {
        throw TransactionException("Failed to create savepoint: " + std::string(e.what()), e.errorCode());
    }
}

Transaction::Savepoint::~Savepoint() {
    // If still active, release (commit) the savepoint
    // Note: Unlike transactions, savepoints default to commit
    // This is because savepoints are typically used for partial work
    // that should be kept unless explicitly rolled back
    if (active_ && conn_) {
        try {
            conn_->execute("RELEASE SAVEPOINT " + name_);
        } catch (...) {
            // Ignore errors in destructor
        }
    }
}

Transaction::Savepoint::Savepoint(Savepoint&& other) noexcept
    : conn_(other.conn_)
    , name_(std::move(other.name_))
    , active_(other.active_)
{
    other.conn_ = nullptr;
    other.active_ = false;
}

Transaction::Savepoint& Transaction::Savepoint::operator=(Savepoint&& other) noexcept {
    if (this != &other) {
        if (active_ && conn_) {
            try {
                conn_->execute("RELEASE SAVEPOINT " + name_);
            } catch (...) {}
        }

        conn_ = other.conn_;
        name_ = std::move(other.name_);
        active_ = other.active_;
        other.conn_ = nullptr;
        other.active_ = false;
    }
    return *this;
}

void Transaction::Savepoint::release() {
    if (!active_) {
        throw TransactionException("Savepoint already ended");
    }

    try {
        conn_->execute("RELEASE SAVEPOINT " + name_);
        active_ = false;
    } catch (const QueryException& e) {
        throw TransactionException("Failed to release savepoint: " + std::string(e.what()), e.errorCode());
    }
}

void Transaction::Savepoint::rollback() {
    if (!active_) {
        throw TransactionException("Savepoint already ended");
    }

    try {
        conn_->execute("ROLLBACK TO SAVEPOINT " + name_);
        // After rollback, savepoint still exists; release it
        conn_->execute("RELEASE SAVEPOINT " + name_);
        active_ = false;
    } catch (const QueryException& e) {
        throw TransactionException("Failed to rollback to savepoint: " + std::string(e.what()), e.errorCode());
    }
}

} // namespace sqlite3db
