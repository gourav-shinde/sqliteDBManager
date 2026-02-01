/**
 * @file transaction.hpp
 * @brief RAII transaction management for ACID compliance
 *
 * INDUSTRY PRACTICE #9: ACID Transactions
 * =========================================
 * ACID stands for:
 * - Atomicity: All operations succeed or all fail (no partial updates)
 * - Consistency: Database moves from one valid state to another
 * - Isolation: Concurrent transactions don't interfere
 * - Durability: Committed changes survive crashes
 *
 * INDUSTRY PRACTICE #10: Scoped Transaction Guards
 * =================================================
 * The RAII pattern for transactions is critical:
 *
 *   void transferMoney(Connection& db, int from, int to, double amount) {
 *       auto txn = db.beginTransaction();  // BEGIN TRANSACTION
 *
 *       // If any of these throw, transaction auto-rolls back
 *       db.execute("UPDATE accounts SET balance = balance - ? WHERE id = ?", amount, from);
 *       db.execute("UPDATE accounts SET balance = balance + ? WHERE id = ?", amount, to);
 *
 *       txn.commit();  // Only commits if we reach here
 *   }  // If commit() not called, destructor does ROLLBACK
 *
 * This pattern makes it impossible to forget to rollback on error!
 *
 * INDUSTRY PRACTICE #11: Savepoints for Nested Transactions
 * ===========================================================
 * SQLite doesn't support true nested transactions, but savepoints
 * provide similar functionality. Useful for:
 * - Partial rollbacks within a larger transaction
 * - Testing parts of a transaction
 * - Implementing retry logic for specific operations
 */

#pragma once

#include <string>
#include <sqlite3.h>
#include "exceptions.hpp"

namespace sqlite3db {

// Forward declaration
class Connection;

/**
 * @brief Transaction isolation level
 *
 * SQLite supports these transaction types:
 * - DEFERRED: Locks acquired on first access (default)
 * - IMMEDIATE: Write lock acquired immediately
 * - EXCLUSIVE: Complete exclusive lock
 */
enum class TransactionType {
    Deferred,   // Lock on first access (default, good for reads)
    Immediate,  // Lock immediately (good for writes)
    Exclusive   // Exclusive lock (for exclusive access)
};

/**
 * @brief RAII guard for database transactions
 *
 * Usage:
 *   {
 *       Transaction txn = conn.beginTransaction();
 *       conn.execute("INSERT INTO ...");
 *       conn.execute("UPDATE ...");
 *       txn.commit();  // Explicit commit
 *   }  // If commit() not called, destructor rolls back
 *
 * The destructor behavior ensures that:
 * - Exceptions don't leave transactions hanging
 * - Early returns don't leave transactions hanging
 * - Forgetting to commit rolls back (safe default)
 */
class Transaction {
public:
    /**
     * @brief Begin a new transaction
     * @param conn Database connection
     * @param type Transaction type (locking strategy)
     * @throws TransactionException if BEGIN fails
     */
    explicit Transaction(Connection& conn,
                        TransactionType type = TransactionType::Deferred);

    /**
     * @brief Destructor - rolls back if not committed
     *
     * IMPORTANT: Destructors should not throw exceptions.
     * If rollback fails here, we log the error but don't throw.
     */
    ~Transaction();

    // Non-copyable (would break transaction semantics)
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    // Moveable
    Transaction(Transaction&& other) noexcept;
    Transaction& operator=(Transaction&& other) noexcept;

    /**
     * @brief Commit the transaction
     * @throws TransactionException if COMMIT fails
     *
     * After commit(), the destructor does nothing.
     * Commit failures can happen (e.g., constraint violations).
     */
    void commit();

    /**
     * @brief Explicitly rollback the transaction
     * @throws TransactionException if ROLLBACK fails
     *
     * Call this to explicitly rollback. Otherwise, the destructor
     * will rollback automatically.
     */
    void rollback();

    /**
     * @brief Check if transaction is still active
     */
    bool isActive() const { return active_; }

    /**
     * @brief Create a savepoint for partial rollback
     * @param name Savepoint name
     * @return Savepoint guard (RAII)
     *
     * Savepoints allow partial rollback within a transaction:
     *   Transaction txn = conn.beginTransaction();
     *   conn.execute("INSERT A");
     *   {
     *       Savepoint sp = txn.savepoint("sp1");
     *       conn.execute("INSERT B");
     *       sp.rollback();  // Only INSERT B is rolled back
     *   }
     *   txn.commit();  // INSERT A is committed
     */
    class Savepoint;
    Savepoint savepoint(const std::string& name);

private:
    Connection* conn_;
    bool active_ = true;  // True until commit/rollback
};

/**
 * @brief RAII guard for savepoints (nested transaction simulation)
 */
class Transaction::Savepoint {
public:
    Savepoint(Connection& conn, const std::string& name);
    ~Savepoint();

    Savepoint(const Savepoint&) = delete;
    Savepoint& operator=(const Savepoint&) = delete;
    Savepoint(Savepoint&& other) noexcept;
    Savepoint& operator=(Savepoint&& other) noexcept;

    /**
     * @brief Release the savepoint (commit sub-transaction)
     */
    void release();

    /**
     * @brief Rollback to this savepoint
     */
    void rollback();

    bool isActive() const { return active_; }

private:
    Connection* conn_;
    std::string name_;
    bool active_ = true;
};

/**
 * @brief Helper for executing operations in a transaction
 *
 * INDUSTRY PRACTICE: Functional Transaction Wrapper
 * Encapsulate the try/commit/catch/rollback pattern:
 *
 *   auto result = withTransaction(conn, [&](Transaction& txn) {
 *       conn.execute("INSERT ...");
 *       conn.execute("UPDATE ...");
 *       return "success";
 *   });  // Automatically commits on success, rolls back on exception
 */
template<typename Func>
auto withTransaction(Connection& conn, Func&& func) -> decltype(func(std::declval<Transaction&>())) {
    Transaction txn(conn);
    auto result = func(txn);
    txn.commit();
    return result;
}

// Void specialization
template<typename Func>
auto withTransactionVoid(Connection& conn, Func&& func) -> void {
    Transaction txn(conn);
    func(txn);
    txn.commit();
}

} // namespace sqlite3db
