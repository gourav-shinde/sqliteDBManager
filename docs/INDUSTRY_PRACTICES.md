# Industry Best Practices for Database Handling in C++

This document explains **why** each practice matters, **what problems** it solves, and **what happens** if you ignore it.

---

## Table of Contents

1. [RAII (Resource Acquisition Is Initialization)](#1-raii-resource-acquisition-is-initialization)
2. [Prepared Statements](#2-prepared-statements)
3. [Transaction Management](#3-transaction-management)
4. [Schema Migrations](#4-schema-migrations)
5. [Schema Validation](#5-schema-validation)
6. [Repository Pattern](#6-repository-pattern)
7. [Query Builder Pattern](#7-query-builder-pattern)
8. [Batch Operations](#8-batch-operations)
9. [Exception Hierarchy](#9-exception-hierarchy)
10. [Connection Configuration](#10-connection-configuration)

---

## 1. RAII (Resource Acquisition Is Initialization)

### What is it?
RAII ties resource lifetime to object lifetime. When an object is created, it acquires a resource. When the object is destroyed, it releases the resource.

### Why does it matter?

**The Problem Without RAII:**
```cpp
void badExample() {
    sqlite3* db;
    sqlite3_open("test.db", &db);

    // Do some work...
    if (someCondition) {
        return;  // LEAK! Database connection never closed
    }

    // Do more work...
    if (error) {
        throw std::runtime_error("oops");  // LEAK! Exception skips cleanup
    }

    sqlite3_close(db);  // Only reached in happy path
}
```

**Problems:**
- Early returns leak resources
- Exceptions leak resources
- You must remember to close every resource manually
- In complex code, it's easy to miss cleanup paths

**The Solution With RAII:**
```cpp
void goodExample() {
    Connection conn("test.db");  // Resource acquired in constructor

    if (someCondition) {
        return;  // SAFE! Destructor called, connection closed
    }

    if (error) {
        throw std::runtime_error("oops");  // SAFE! Stack unwinding calls destructor
    }

}  // Destructor automatically closes connection
```

### How we implement it:

**File: `include/sqlite3db/connection.hpp`**
```cpp
class Connection {
public:
    // Constructor ACQUIRES the resource
    explicit Connection(const std::string& dbPath) {
        int result = sqlite3_open(dbPath.c_str(), &db_);
        if (result != SQLITE_OK) {
            throw ConnectionException("Failed to open database");
        }
    }

    // Destructor RELEASES the resource - GUARANTEED to run
    ~Connection() {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    // Delete copy operations to prevent double-close bugs
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Allow move operations for flexibility
    Connection(Connection&& other) noexcept : db_(other.db_) {
        other.db_ = nullptr;  // Source no longer owns the resource
    }

private:
    sqlite3* db_ = nullptr;
};
```

### Why delete copy constructor?

```cpp
// If copying was allowed:
Connection conn1("test.db");
Connection conn2 = conn1;  // Both point to same sqlite3*

// When conn2 is destroyed: sqlite3_close(db_) called
// When conn1 is destroyed: sqlite3_close(db_) called AGAIN on freed memory!
// Result: CRASH or undefined behavior
```

### Real-world impact:
- **Memory leaks** in long-running servers cause out-of-memory crashes
- **Connection leaks** exhaust database connection limits
- **File handle leaks** cause "too many open files" errors

---

## 2. Prepared Statements

### What is it?
Prepared statements separate SQL structure from data values. The database compiles the SQL once, then you bind values separately.

### Why does it matter?

**The Problem - SQL Injection:**
```cpp
// DANGEROUS CODE - Never do this!
std::string username = getUserInput();  // User enters: ' OR '1'='1
std::string sql = "SELECT * FROM users WHERE name = '" + username + "'";

// Resulting SQL:
// SELECT * FROM users WHERE name = '' OR '1'='1'
// This returns ALL users - authentication bypassed!

// Even worse, user enters: '; DROP TABLE users; --
// SELECT * FROM users WHERE name = ''; DROP TABLE users; --'
// Your entire users table is deleted!
```

**SQL Injection is:**
- #1 on OWASP Top 10 vulnerabilities
- Responsible for major data breaches (Sony, LinkedIn, etc.)
- Completely preventable with prepared statements

**The Solution - Prepared Statements:**
```cpp
// SAFE - Values are never interpreted as SQL
auto stmt = conn.prepare("SELECT * FROM users WHERE name = ?");
stmt.bind(1, username);  // Even if username contains SQL, it's treated as data

// The database sees:
// - SQL structure: SELECT * FROM users WHERE name = ?
// - Data value: "' OR '1'='1" (just a weird string, not SQL code)
```

### How we implement it:

**File: `include/sqlite3db/statement.hpp`**
```cpp
class Statement {
public:
    // Bind different types safely
    Statement& bind(int index, const std::string& value) {
        // SQLITE_TRANSIENT tells SQLite to copy the string
        // This is safe even if the original string is destroyed
        sqlite3_bind_text(stmt_, index, value.c_str(),
                         value.size(), SQLITE_TRANSIENT);
        return *this;
    }

    Statement& bind(int index, int64_t value) {
        sqlite3_bind_int64(stmt_, index, value);
        return *this;
    }

    // Fluent interface - return *this for chaining
    // stmt.bind(1, "Alice").bind(2, 30).bind(3, "alice@test.com").execute();
};
```

### Additional benefits:
1. **Performance**: SQL is compiled once, reused many times
2. **Type safety**: Compiler catches type mismatches
3. **Cleaner code**: No string concatenation mess

### Real-world impact:
- SQL injection has caused billions of dollars in damages
- Companies have faced lawsuits and regulatory fines
- Careers have ended over preventable SQL injection vulnerabilities

---

## 3. Transaction Management

### What is it?
A transaction groups multiple operations into a single atomic unit. Either ALL operations succeed, or NONE of them do.

### Why does it matter?

**The Problem Without Transactions:**
```cpp
// Transferring $100 from Account A to Account B
void transferMoney(Connection& db, int from, int to, double amount) {
    db.execute("UPDATE accounts SET balance = balance - 100 WHERE id = 1");

    // CRASH HAPPENS HERE - power failure, exception, anything

    db.execute("UPDATE accounts SET balance = balance + 100 WHERE id = 2");
}
// Result: $100 disappeared! Account A lost money, Account B didn't receive it
```

**The Solution With Transactions:**
```cpp
void transferMoney(Connection& db, int from, int to, double amount) {
    Transaction txn = db.beginTransaction();  // BEGIN TRANSACTION

    db.execute("UPDATE accounts SET balance = balance - 100 WHERE id = 1");

    // If crash happens here, transaction is rolled back
    // Account A still has its money

    db.execute("UPDATE accounts SET balance = balance + 100 WHERE id = 2");

    txn.commit();  // COMMIT - both changes are now permanent
}
```

### ACID Properties:

| Property | Meaning | Example |
|----------|---------|---------|
| **Atomicity** | All or nothing | Both accounts update, or neither does |
| **Consistency** | Valid state to valid state | Total money in system stays constant |
| **Isolation** | Transactions don't interfere | Two transfers don't corrupt each other |
| **Durability** | Committed = permanent | Power failure after commit doesn't lose data |

### How we implement it:

**File: `include/sqlite3db/transaction.hpp`**
```cpp
class Transaction {
public:
    explicit Transaction(Connection& conn) : conn_(&conn), active_(true) {
        conn_->execute("BEGIN TRANSACTION");
    }

    // CRITICAL: Destructor rolls back if not committed
    ~Transaction() {
        if (active_) {
            try {
                conn_->execute("ROLLBACK");
            } catch (...) {
                // Never throw from destructor
            }
        }
    }

    void commit() {
        conn_->execute("COMMIT");
        active_ = false;  // Prevent rollback in destructor
    }

    void rollback() {
        conn_->execute("ROLLBACK");
        active_ = false;
    }

private:
    Connection* conn_;
    bool active_;
};
```

### Why auto-rollback in destructor?

```cpp
void processOrder(Connection& db, const Order& order) {
    Transaction txn = db.beginTransaction();

    db.execute("INSERT INTO orders ...");
    db.execute("UPDATE inventory ...");

    validateOrder(order);  // This throws an exception!

    db.execute("INSERT INTO shipping ...");

    txn.commit();
}
// Exception causes stack unwinding
// Transaction destructor is called
// ROLLBACK happens automatically
// Database is in consistent state - no partial order
```

### Savepoints - Partial Rollback:

```cpp
Transaction txn = db.beginTransaction();

db.execute("INSERT INTO audit_log ...");  // Always keep this

{
    auto sp = txn.savepoint("optional_work");

    try {
        db.execute("INSERT INTO analytics ...");  // Optional
        sp.release();  // Commit savepoint
    } catch (...) {
        sp.rollback();  // Only analytics insert is rolled back
    }
}

txn.commit();  // Audit log is committed regardless
```

### Real-world impact:
- Banks: Money must never appear or disappear
- E-commerce: Orders must be complete or not exist
- Healthcare: Patient records must be consistent

---

## 4. Schema Migrations

### What is it?
Migrations are versioned, incremental changes to your database schema. Each migration has a version number and transforms the database from version N to version N+1.

### Why does it matter?

**The Problem Without Migrations:**

*Day 1:* You create a database with users table
```sql
CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT);
```

*Day 30:* You need to add email column
- How do you update production databases?
- How do you update developer machines?
- How do you update the test server?
- What if someone joins the team with an old database?

*Day 60:* You need to add an index, rename a column, add another table...

**Without migrations, you end up with:**
- Manual SQL scripts scattered everywhere
- "Did you run the alter table script?"
- Different databases in different states
- Fear of making schema changes

**The Solution - Migrations:**

```cpp
MigrationManager migrations;

migrations.add(1, "Create users table", [](Connection& db) {
    db.execute(R"(
        CREATE TABLE users (
            id INTEGER PRIMARY KEY,
            name TEXT NOT NULL
        )
    )");
});

migrations.add(2, "Add email to users", [](Connection& db) {
    db.execute("ALTER TABLE users ADD COLUMN email TEXT");
});

migrations.add(3, "Add email index", [](Connection& db) {
    db.execute("CREATE INDEX idx_users_email ON users(email)");
});

// On application startup:
migrations.apply(conn);  // Automatically runs any pending migrations
```

### How it works:

1. Database stores current version in `__migrations` table
2. On startup, compare database version to latest migration
3. Run all migrations from current+1 to latest
4. Each migration runs in a transaction (atomic)

**File: `include/sqlite3db/migration.hpp`**
```cpp
class MigrationManager {
public:
    void apply(Connection& conn) {
        ensureMigrationTable(conn);
        int current = currentVersion(conn);

        for (auto& [version, migration] : migrations_) {
            if (version > current) {
                Transaction txn(conn);
                migration.up(conn);
                recordMigration(conn, version);
                txn.commit();
                // If migration fails, transaction rolls back
                // Database stays at previous version
            }
        }
    }
};
```

### Benefits:
- **Reproducible**: Any database can be brought to current state
- **Version controlled**: Migrations are code, stored in git
- **Reversible**: Optional down() functions for rollback
- **Team-friendly**: Everyone runs the same migrations

### Real-world impact:
- Deployment becomes automated and reliable
- New team members get correct schema automatically
- You can confidently refactor database design

---

## 5. Schema Validation

### What is it?
Runtime verification that the database schema matches what your application expects.

### Why does it matter?

**The Problem:**
- Someone manually modified the production database
- A migration failed silently
- Wrong database file is being used
- Schema mismatch between code and database

**Without Validation:**
```cpp
// Application starts
// Queries start failing with cryptic errors:
// "no such column: email"
// "no such table: orders"
// Hours of debugging to figure out what's wrong
```

**With Validation:**
```cpp
SchemaValidator validator;
validator
    .requireTable("users")
    .requireColumn("users", "id", "INTEGER")
    .requireColumn("users", "email", "TEXT")
    .requireNotNull("users", "email")
    .requireIndex("users", "idx_users_email");

// On startup, BEFORE any business logic:
auto errors = validator.validate(conn);
if (!errors.empty()) {
    for (const auto& err : errors) {
        std::cerr << "SCHEMA ERROR: " << err.message << "\n";
    }
    std::exit(1);  // Fail fast with clear message
}
```

### How we implement it:

**File: `src/migration.cpp`**
```cpp
std::vector<ValidationError> SchemaValidator::validate(Connection& conn) const {
    std::vector<ValidationError> errors;

    // Check tables exist
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

    // Check columns exist with correct types
    for (const auto& req : columnRequirements_) {
        auto stmt = conn.prepare("PRAGMA table_info(" + req.tableName + ")");
        bool found = false;
        while (stmt.step()) {
            if (stmt.columnString(1) == req.columnName) {
                found = true;
                // Verify type if specified
                if (!req.expectedType.empty()) {
                    std::string actualType = stmt.columnString(2);
                    if (actualType != req.expectedType) {
                        errors.push_back({
                            "wrong_type",
                            "Column " + req.columnName + " has wrong type"
                        });
                    }
                }
            }
        }
        if (!found) {
            errors.push_back({
                "missing_column",
                "Required column '" + req.columnName + "' not found"
            });
        }
    }

    return errors;
}
```

### Real-world impact:
- **Fail fast**: Find problems at startup, not in production
- **Clear errors**: Know exactly what's wrong
- **Confidence**: Guaranteed schema matches expectations

---

## 6. Repository Pattern

### What is it?
An abstraction layer between your business logic and data access code. The repository acts like a collection of domain objects.

### Why does it matter?

**The Problem Without Repository:**
```cpp
// SQL scattered throughout business logic
void processOrder(Connection& db, int userId, std::vector<Item> items) {
    // Business logic mixed with SQL
    auto stmt = db.prepare("SELECT * FROM users WHERE id = ?");
    stmt.bind(1, userId);
    if (!stmt.step()) {
        throw std::runtime_error("User not found");
    }
    std::string email = stmt.columnString(2);

    // More SQL in business logic
    for (const auto& item : items) {
        auto stmt2 = db.prepare("INSERT INTO order_items ...");
        // binding code...
    }

    // Testing this function requires a real database
    // Changing database requires changing business logic
    // SQL details leak everywhere
}
```

**The Solution With Repository:**
```cpp
class UserRepository {
public:
    std::optional<User> findById(int64_t id);
    std::vector<User> findByAge(int minAge, int maxAge);
    int64_t create(const User& user);
    bool update(const User& user);
    bool deleteById(int64_t id);
};

// Business logic is clean
void processOrder(UserRepository& users, OrderRepository& orders,
                  int userId, std::vector<Item> items) {
    auto user = users.findById(userId);
    if (!user) {
        throw std::runtime_error("User not found");
    }

    Order order;
    order.userId = userId;
    order.items = items;
    orders.create(order);

    sendEmail(user->email, "Order confirmed");
}

// Testing: Just mock the repository
class MockUserRepository : public IUserRepository {
    std::optional<User> findById(int64_t id) override {
        return User{id, "Test User", "test@example.com"};
    }
};
```

### How we implement it:

**File: `include/sqlite3db/repository.hpp`**
```cpp
template<typename T>
class Repository {
public:
    Repository(Connection& conn, const std::string& tableName)
        : conn_(conn), tableName_(tableName) {}

    std::optional<T> findById(int64_t id) {
        auto stmt = conn_.prepare(
            "SELECT * FROM " + tableName_ + " WHERE id = ?");
        stmt.bind(1, id);
        if (stmt.step()) {
            return fromRow(stmt);  // Subclass converts row to entity
        }
        return std::nullopt;
    }

    std::vector<T> findAll() {
        auto stmt = conn_.prepare("SELECT * FROM " + tableName_);
        std::vector<T> results;
        while (stmt.step()) {
            results.push_back(fromRow(stmt));
        }
        return results;
    }

protected:
    // Subclass implements this to map database row to domain object
    virtual T fromRow(Statement& stmt) = 0;

    Connection& conn_;
    std::string tableName_;
};

// Usage:
class UserRepository : public Repository<User> {
public:
    UserRepository(Connection& conn) : Repository(conn, "users") {}

protected:
    User fromRow(Statement& stmt) override {
        return User{
            stmt.columnInt64(0),    // id
            stmt.columnString(1),   // name
            stmt.columnString(2),   // email
            stmt.columnInt(3)       // age
        };
    }
};
```

### Benefits:
1. **Testability**: Mock repositories for unit tests
2. **Single Responsibility**: Repository handles persistence only
3. **Abstraction**: Business logic doesn't know about SQL
4. **Maintainability**: Change data access in one place

---

## 7. Query Builder Pattern

### What is it?
A fluent interface for constructing SQL queries programmatically instead of string concatenation.

### Why does it matter?

**The Problem With String Concatenation:**
```cpp
std::string sql = "SELECT ";
sql += columns;
sql += " FROM users";
if (!whereClause.empty()) {
    sql += " WHERE " + whereClause;  // SQL injection risk!
}
if (!orderBy.empty()) {
    sql += " ORDER BY " + orderBy;
}
// Easy to mess up spacing, forget keywords, etc.
```

**The Solution - Query Builder:**
```cpp
auto results = QueryBuilder(conn, "users")
    .select({"name", "email", "age"})
    .where("age", ">", 18)
    .where("active", "=", true)
    .whereNotNull("email")
    .orderBy("name")
    .limit(10)
    .fetchAll();
```

### How we implement it:

**File: `include/sqlite3db/repository.hpp`**
```cpp
class QueryBuilder {
public:
    QueryBuilder& where(const std::string& column,
                        const std::string& op,
                        const Value& value) {
        // Store condition for later
        whereClauses_.push_back(column + " " + op + " ?");
        whereValues_.push_back(value);
        return *this;  // Enable chaining
    }

    std::string toSql() const {
        std::ostringstream sql;
        sql << "SELECT " << selectClause_ << " FROM " << table_;

        if (!whereClauses_.empty()) {
            sql << " WHERE ";
            for (size_t i = 0; i < whereClauses_.size(); ++i) {
                if (i > 0) sql << " AND ";
                sql << whereClauses_[i];
            }
        }

        if (!orderByClause_.empty()) {
            sql << " ORDER BY " << orderByClause_;
        }

        return sql.str();
    }

    std::vector<std::vector<Value>> fetchAll() {
        auto stmt = conn_.prepare(toSql());

        // Bind all values safely
        for (size_t i = 0; i < whereValues_.size(); ++i) {
            stmt.bind(i + 1, whereValues_[i]);
        }

        // Fetch results...
    }
};
```

### Benefits:
1. **Type safety**: Compiler catches errors
2. **SQL injection safe**: Values are always bound
3. **Readable**: Intent is clear
4. **Composable**: Build queries dynamically

---

## 8. Batch Operations

### What is it?
Grouping multiple database operations into a single transaction instead of executing them one by one.

### Why does it matter?

**The Problem - One Transaction Per Insert:**
```cpp
for (const auto& user : users) {  // 10,000 users
    db.execute("INSERT INTO users (name) VALUES (?)", user.name);
    // Each insert: BEGIN TRANSACTION, INSERT, COMMIT
    // 10,000 transactions = 10,000 disk syncs = SLOW
}
// Takes: ~30 seconds
```

**The Solution - Batch Insert:**
```cpp
Transaction txn = db.beginTransaction();  // One BEGIN

for (const auto& user : users) {  // 10,000 users
    db.execute("INSERT INTO users (name) VALUES (?)", user.name);
    // Each insert is just an INSERT, no transaction overhead
}

txn.commit();  // One COMMIT, one disk sync
// Takes: ~0.3 seconds (100x faster!)
```

### How we implement it:

**File: `include/sqlite3db/repository.hpp`**
```cpp
class BatchInsertBuilder {
public:
    BatchInsertBuilder(Connection& conn, const std::string& table,
                       const std::vector<std::string>& columns)
        : conn_(conn), table_(table), columns_(columns) {}

    BatchInsertBuilder& addRow(const std::vector<Value>& values) {
        rows_.push_back(values);
        return *this;
    }

    int64_t execute() {
        // Single transaction for all rows
        Transaction txn(conn_);

        // Prepare once, execute many times
        auto stmt = conn_.prepare(buildInsertSql());

        for (const auto& row : rows_) {
            for (size_t i = 0; i < row.size(); ++i) {
                stmt.bind(i + 1, row[i]);
            }
            stmt.execute();
            stmt.reset();
            stmt.clearBindings();
        }

        txn.commit();
        return rows_.size();
    }
};

// Usage:
BatchInsertBuilder batch(conn, "products", {"name", "price"});
for (const auto& product : products) {
    batch.addRow({product.name, product.price});
}
batch.execute();  // All products inserted in one transaction
```

### Performance comparison:

| Method | 10,000 rows | 100,000 rows |
|--------|-------------|--------------|
| Individual inserts | 30 seconds | 5 minutes |
| Batch insert | 0.3 seconds | 3 seconds |

### Real-world impact:
- Import operations complete in seconds instead of minutes
- Server resources are used efficiently
- User experience is dramatically better

---

## 9. Exception Hierarchy

### What is it?
A structured set of exception classes that provide specific error information for different failure modes.

### Why does it matter?

**The Problem With Generic Exceptions:**
```cpp
try {
    doSomeDatabaseWork();
} catch (const std::exception& e) {
    // What went wrong?
    // - Connection failed?
    // - Query syntax error?
    // - Constraint violation?
    // - Transaction failed?
    // We don't know! Can't handle appropriately.
    std::cerr << "Something went wrong: " << e.what() << "\n";
}
```

**The Solution - Exception Hierarchy:**
```cpp
try {
    doSomeDatabaseWork();
} catch (const ConstraintException& e) {
    // Duplicate email, foreign key violation, etc.
    // User error - show friendly message
    return "That email is already taken";
} catch (const QueryException& e) {
    // SQL error - log for debugging
    log.error("Query failed: " + e.sql());
    return "Database error, please try again";
} catch (const ConnectionException& e) {
    // Infrastructure problem - alert ops team
    alertOpsTeam("Database connection failed");
    return "Service temporarily unavailable";
} catch (const DatabaseException& e) {
    // Catch-all for any database error
    log.error("Database error: " + std::string(e.what()));
    return "An error occurred";
}
```

### How we implement it:

**File: `include/sqlite3db/exceptions.hpp`**
```cpp
// Base exception - all database errors inherit from this
class DatabaseException : public std::exception {
public:
    explicit DatabaseException(std::string message, int errorCode = 0)
        : message_(std::move(message)), errorCode_(errorCode) {
        // Include error code for debugging
        if (errorCode_ != 0) {
            fullMessage_ = message_ + " (SQLite error: " +
                          std::to_string(errorCode_) + ")";
        } else {
            fullMessage_ = message_;
        }
    }

    const char* what() const noexcept override {
        return fullMessage_.c_str();
    }

    int errorCode() const noexcept { return errorCode_; }

protected:
    std::string message_;
    std::string fullMessage_;
    int errorCode_;
};

// Specific exceptions for different failure modes
class ConnectionException : public DatabaseException {
public:
    explicit ConnectionException(const std::string& message, int errorCode = 0)
        : DatabaseException("Connection error: " + message, errorCode) {}
};

class QueryException : public DatabaseException {
public:
    QueryException(const std::string& message, const std::string& sql,
                   int errorCode = 0)
        : DatabaseException("Query error: " + message, errorCode), sql_(sql) {
        fullMessage_ += "\nSQL: " + sql_;  // Include SQL for debugging
    }

    const std::string& sql() const noexcept { return sql_; }

private:
    std::string sql_;
};

class ConstraintException : public DatabaseException {
    // Unique violations, foreign key violations, check constraints
};

class TransactionException : public DatabaseException {
    // Transaction begin/commit/rollback failures
};

class MigrationException : public DatabaseException {
public:
    MigrationException(const std::string& message, int version)
        : DatabaseException("Migration error at v" + std::to_string(version) +
                           ": " + message), version_(version) {}

    int version() const noexcept { return version_; }

private:
    int version_;
};
```

### Benefits:
1. **Appropriate handling**: Different errors need different responses
2. **Rich context**: Error codes, SQL statements, version numbers
3. **Flexible catching**: Catch specific or general as needed
4. **Debugging**: Full information for troubleshooting

---

## 10. Connection Configuration

### What is it?
A structured way to configure database connection options with sensible defaults.

### Why does it matter?

**The Problem With Many Parameters:**
```cpp
// Constructor with many parameters - hard to read and maintain
Connection(const std::string& path, bool enableWAL, bool enableFK,
           int timeout, bool readOnly, bool create, bool extendedCodes);

// Usage - what does each bool mean?
Connection conn("test.db", true, true, 5000, false, true, true);
```

**The Solution - Options Struct:**
```cpp
struct ConnectionOptions {
    bool enableWAL = true;           // Sensible default
    bool enableForeignKeys = true;   // Sensible default
    int busyTimeoutMs = 5000;        // Sensible default
    bool readOnly = false;
    bool createIfNotExists = true;
    bool extendedResultCodes = true;
};

// Usage - clear and self-documenting
ConnectionOptions opts;
opts.enableWAL = true;
opts.busyTimeoutMs = 10000;
Connection conn("test.db", opts);

// Or use all defaults
Connection conn("test.db");
```

### Important SQLite options explained:

**WAL (Write-Ahead Logging):**
```cpp
opts.enableWAL = true;
```
- Default SQLite uses rollback journal (readers block writers)
- WAL mode: readers and writer can work simultaneously
- Essential for multi-threaded applications
- Improves performance significantly

**Foreign Keys:**
```cpp
opts.enableForeignKeys = true;
```
- SQLite has foreign keys OFF by default (legacy reasons)
- Must enable explicitly for referential integrity
- Prevents orphaned records

**Busy Timeout:**
```cpp
opts.busyTimeoutMs = 5000;
```
- How long to wait when database is locked
- Without timeout: immediate failure on contention
- With timeout: SQLite retries for N milliseconds

### How we implement it:

**File: `src/connection.cpp`**
```cpp
void Connection::applyOptions(const ConnectionOptions& options) {
    // Extended error codes for better debugging
    if (options.extendedResultCodes) {
        sqlite3_extended_result_codes(db_, 1);
    }

    // Busy timeout for handling contention
    sqlite3_busy_timeout(db_, options.busyTimeoutMs);

    // Foreign keys must be enabled per-connection
    if (options.enableForeignKeys) {
        execute("PRAGMA foreign_keys = ON");
    }

    // WAL mode for better concurrency
    if (options.enableWAL) {
        execute("PRAGMA journal_mode = WAL");
    }
}
```

---

## Summary: Why These Practices Matter

| Practice | Prevents | Enables |
|----------|----------|---------|
| RAII | Resource leaks, crashes | Automatic cleanup, exception safety |
| Prepared Statements | SQL injection attacks | Security, performance |
| Transactions | Data corruption, inconsistency | ACID compliance, atomicity |
| Migrations | Schema drift, manual updates | Automated deployment, team sync |
| Schema Validation | Runtime errors, confusion | Early failure, clear messages |
| Repository Pattern | Spaghetti code, testing difficulty | Clean architecture, testability |
| Query Builder | SQL errors, injection | Type safety, readability |
| Batch Operations | Slow imports, timeouts | 100x performance improvement |
| Exception Hierarchy | Generic error handling | Appropriate responses, debugging |
| Configuration Options | Confusing APIs | Clean, self-documenting code |

These practices are not academic theory—they're battle-tested solutions to real problems that have cost companies millions of dollars in security breaches, lost data, and developer time.
