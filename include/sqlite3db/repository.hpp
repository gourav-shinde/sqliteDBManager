/**
 * @file repository.hpp
 * @brief Repository pattern for data access abstraction
 *
 * INDUSTRY PRACTICE #14: Repository Pattern
 * ===========================================
 * The Repository pattern provides an abstraction layer between
 * your business logic and data access:
 *
 * Benefits:
 * - Encapsulates data access logic in one place
 * - Makes business logic easier to test (mock the repository)
 * - Hides database implementation details
 * - Provides a collection-like interface for data
 * - Single responsibility: Repository handles persistence
 *
 * Without Repository:
 *   // Business logic mixed with SQL
 *   auto stmt = conn.prepare("SELECT * FROM users WHERE id = ?");
 *   stmt.bind(1, userId);
 *   if (stmt.step()) {
 *       User user;
 *       user.id = stmt.columnInt64(0);
 *       user.name = stmt.columnString(1);
 *       // ... process user
 *   }
 *
 * With Repository:
 *   // Clean separation
 *   auto user = userRepository.findById(userId);
 *   if (user) {
 *       // ... process user
 *   }
 *
 * INDUSTRY PRACTICE #15: Query Builder Pattern
 * ==============================================
 * For complex queries, building SQL strings is error-prone.
 * A query builder provides a type-safe, fluent interface:
 *
 *   auto users = QueryBuilder(conn)
 *       .select("id", "name", "email")
 *       .from("users")
 *       .where("age", ">", 18)
 *       .where("active", "=", true)
 *       .orderBy("name")
 *       .limit(10)
 *       .execute();
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <sstream>
#include "connection.hpp"
#include "statement.hpp"
#include "transaction.hpp"

namespace sqlite3db {

/**
 * @brief Simple query builder for SELECT statements
 *
 * Provides a fluent interface for building queries:
 *   auto results = QueryBuilder(conn, "users")
 *       .select({"id", "name", "email"})
 *       .where("active = ?", true)
 *       .orderBy("created_at", false)
 *       .limit(10)
 *       .fetchAll();
 */
class QueryBuilder {
public:
    explicit QueryBuilder(Connection& conn, const std::string& table = "");

    // SELECT clause
    QueryBuilder& select(const std::vector<std::string>& columns);
    QueryBuilder& select(const std::string& columns);  // Raw, e.g., "COUNT(*)"
    QueryBuilder& selectAll();

    // FROM clause
    QueryBuilder& from(const std::string& table);

    // WHERE clauses
    QueryBuilder& where(const std::string& column, const std::string& op, const Value& value);
    QueryBuilder& where(const std::string& rawCondition, const Value& value);
    QueryBuilder& whereNull(const std::string& column);
    QueryBuilder& whereNotNull(const std::string& column);
    QueryBuilder& whereIn(const std::string& column, const std::vector<Value>& values);

    // JOIN clauses
    QueryBuilder& join(const std::string& table, const std::string& condition);
    QueryBuilder& leftJoin(const std::string& table, const std::string& condition);

    // ORDER BY
    QueryBuilder& orderBy(const std::string& column, bool ascending = true);

    // LIMIT/OFFSET
    QueryBuilder& limit(int count);
    QueryBuilder& offset(int count);

    // GROUP BY / HAVING
    QueryBuilder& groupBy(const std::string& column);
    QueryBuilder& having(const std::string& condition, const Value& value);

    // Execute and fetch
    std::vector<std::vector<Value>> fetchAll();
    std::optional<std::vector<Value>> fetchOne();
    int64_t count();

    // Get the built SQL (for debugging)
    std::string toSql() const;

private:
    Connection& conn_;
    std::string table_;
    std::string selectClause_ = "*";
    std::vector<std::string> joins_;
    std::vector<std::string> whereClauses_;
    std::vector<Value> whereValues_;
    std::string orderByClause_;
    std::string groupByClause_;
    std::string havingClause_;
    Value havingValue_;
    bool hasHaving_ = false;
    int limit_ = -1;
    int offset_ = -1;
};

/**
 * @brief Insert builder for INSERT statements
 *
 * Usage:
 *   InsertBuilder(conn, "users")
 *       .value("name", "John")
 *       .value("email", "john@example.com")
 *       .value("age", 30)
 *       .execute();
 */
class InsertBuilder {
public:
    InsertBuilder(Connection& conn, const std::string& table);

    /**
     * @brief Add a column-value pair
     */
    InsertBuilder& value(const std::string& column, const Value& val);

    /**
     * @brief Execute the insert
     * @return Last insert row ID
     */
    int64_t execute();

    /**
     * @brief Execute insert or update on conflict
     *
     * INDUSTRY PRACTICE: Upsert (INSERT OR REPLACE)
     * Common pattern for "insert if new, update if exists"
     */
    int64_t upsert();

    std::string toSql() const;

private:
    Connection& conn_;
    std::string table_;
    std::vector<std::string> columns_;
    std::vector<Value> values_;
};

/**
 * @brief Batch insert builder for efficient bulk inserts
 *
 * INDUSTRY PRACTICE #16: Batch Operations
 * =========================================
 * Inserting rows one at a time is slow due to transaction overhead.
 * Batch inserts group multiple rows into a single transaction:
 *
 * Slow (N transactions):
 *   for (auto& user : users) {
 *       db.execute("INSERT ...", user.name);  // Each is a transaction
 *   }
 *
 * Fast (1 transaction):
 *   BatchInsertBuilder batch(conn, "users", {"name", "email"});
 *   for (auto& user : users) {
 *       batch.addRow({user.name, user.email});
 *   }
 *   batch.execute();  // Single transaction for all rows
 *
 * Performance difference: 10-100x faster for large inserts!
 */
class BatchInsertBuilder {
public:
    BatchInsertBuilder(Connection& conn, const std::string& table,
                       const std::vector<std::string>& columns);

    /**
     * @brief Add a row of values (must match column count)
     */
    BatchInsertBuilder& addRow(const std::vector<Value>& values);

    /**
     * @brief Execute the batch insert
     * @return Number of rows inserted
     */
    int64_t execute();

    /**
     * @brief Set batch size for very large inserts
     *
     * For millions of rows, break into batches to avoid
     * excessive memory usage and long-running transactions.
     */
    BatchInsertBuilder& setBatchSize(size_t size);

    /**
     * @brief Clear all rows (for reuse)
     */
    BatchInsertBuilder& clear();

    size_t rowCount() const { return rows_.size(); }

private:
    Connection& conn_;
    std::string table_;
    std::vector<std::string> columns_;
    std::vector<std::vector<Value>> rows_;
    size_t batchSize_ = 1000;  // Default: 1000 rows per batch
};

/**
 * @brief Base class for type-safe repositories
 *
 * Usage:
 *   struct User {
 *       int64_t id;
 *       std::string name;
 *       std::string email;
 *   };
 *
 *   class UserRepository : public Repository<User> {
 *   public:
 *       UserRepository(Connection& conn) : Repository(conn, "users") {}
 *
 *   protected:
 *       User fromRow(Statement& stmt) override {
 *           return User{
 *               stmt.columnInt64(0),
 *               stmt.columnString(1),
 *               stmt.columnString(2)
 *           };
 *       }
 *
 *       void bindForInsert(Statement& stmt, const User& user) override {
 *           stmt.bind(1, user.name).bind(2, user.email);
 *       }
 *   };
 */
template<typename T>
class Repository {
public:
    Repository(Connection& conn, const std::string& tableName)
        : conn_(conn), tableName_(tableName) {}

    virtual ~Repository() = default;

    /**
     * @brief Find entity by primary key
     */
    std::optional<T> findById(int64_t id) {
        auto stmt = conn_.prepare(
            "SELECT * FROM " + tableName_ + " WHERE id = ?");
        stmt.bind(1, id);
        if (stmt.step()) {
            return fromRow(stmt);
        }
        return std::nullopt;
    }

    /**
     * @brief Get all entities
     */
    std::vector<T> findAll() {
        auto stmt = conn_.prepare("SELECT * FROM " + tableName_);
        std::vector<T> results;
        while (stmt.step()) {
            results.push_back(fromRow(stmt));
        }
        return results;
    }

    /**
     * @brief Delete by primary key
     * @return true if entity was deleted
     */
    bool deleteById(int64_t id) {
        auto stmt = conn_.prepare(
            "DELETE FROM " + tableName_ + " WHERE id = ?");
        stmt.bind(1, id);
        stmt.execute();
        return conn_.changes() > 0;
    }

    /**
     * @brief Count all entities
     */
    int64_t count() {
        auto stmt = conn_.prepare(
            "SELECT COUNT(*) FROM " + tableName_);
        stmt.step();
        return stmt.columnInt64(0);
    }

    /**
     * @brief Check if entity exists
     */
    bool exists(int64_t id) {
        auto stmt = conn_.prepare(
            "SELECT 1 FROM " + tableName_ + " WHERE id = ? LIMIT 1");
        stmt.bind(1, id);
        return stmt.step();
    }

protected:
    /**
     * @brief Convert a database row to entity
     * Override this in derived classes
     */
    virtual T fromRow(Statement& stmt) = 0;

    /**
     * @brief Bind entity values for insert
     * Override this in derived classes
     */
    virtual void bindForInsert(Statement& stmt, const T& entity) = 0;

    Connection& conn_;
    std::string tableName_;
};

} // namespace sqlite3db
