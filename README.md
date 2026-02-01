# sqlite3db

A modern C++17 SQLite library demonstrating industry best practices for database handling.

## Features

- **RAII Resource Management** - Connections, statements, and transactions automatically clean up
- **SQL Injection Prevention** - Prepared statements with type-safe parameter binding
- **Transaction Management** - Scoped transactions with automatic rollback on exceptions
- **Schema Migrations** - Version-controlled database schema evolution
- **Schema Validation** - Runtime verification of database structure
- **Repository Pattern** - Clean separation of data access from business logic
- **Query Builder** - Fluent interface for constructing queries
- **Batch Operations** - Efficient bulk inserts (10-100x faster)
- **Exception Hierarchy** - Specific error types for different failure modes

## Quick Start

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt install libsqlite3-dev

# macOS
brew install sqlite3
```

### Build

```bash
# Using Make
make
make run-example
make run-tests

# Using CMake
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON
make
./sqlite3db_example
```

## Usage Examples

### Basic Connection

```cpp
#include <sqlite3db/sqlite3db.hpp>
using namespace sqlite3db;

// File database
Connection conn("myapp.db");

// In-memory database (great for testing)
auto conn = Connection::inMemory();

// With options
ConnectionOptions opts;
opts.enableWAL = true;          // Better concurrent access
opts.enableForeignKeys = true;  // Enforce referential integrity
Connection conn("myapp.db", opts);
```

### Prepared Statements (SQL Injection Prevention)

```cpp
// WRONG - vulnerable to SQL injection!
std::string sql = "SELECT * FROM users WHERE name = '" + userInput + "'";

// CORRECT - always use prepared statements
auto stmt = conn.prepare("SELECT * FROM users WHERE name = ?");
stmt.bind(1, userInput);  // Safe - value is properly escaped

while (stmt.step()) {
    std::cout << stmt.columnString(0) << "\n";
}

// Named parameters for readability
auto stmt = conn.prepare("INSERT INTO users (name, age) VALUES (:name, :age)");
stmt.bind(":name", "Alice").bind(":age", 30).execute();
```

### Transactions

```cpp
// Scoped transaction - auto-rollback on exception
{
    Transaction txn = conn.beginTransaction();

    conn.execute("UPDATE accounts SET balance = balance - 100 WHERE id = 1");
    conn.execute("UPDATE accounts SET balance = balance + 100 WHERE id = 2");

    txn.commit();  // Only commits if we reach here
}  // If commit() not called, destructor rolls back

// Savepoints for partial rollback
Transaction txn = conn.beginTransaction();
conn.execute("INSERT INTO logs (msg) VALUES ('started')");

{
    auto sp = txn.savepoint("risky_op");
    conn.execute("INSERT INTO data VALUES (...)");

    if (somethingWentWrong) {
        sp.rollback();  // Only this insert is rolled back
    }
}

txn.commit();  // Log entry is kept
```

### Schema Migrations

```cpp
MigrationManager migrations;

migrations.add(1, "Create users table", [](Connection& db) {
    db.execute(R"(
        CREATE TABLE users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            email TEXT UNIQUE
        )
    )");
});

migrations.add(2, "Add age column", [](Connection& db) {
    db.execute("ALTER TABLE users ADD COLUMN age INTEGER DEFAULT 0");
});

migrations.add(3, "Add index", [](Connection& db) {
    db.execute("CREATE INDEX idx_users_email ON users(email)");
});

// Apply all pending migrations
migrations.apply(conn);

// Check status
std::cout << "Current version: " << migrations.currentVersion(conn) << "\n";
```

### Schema Validation

```cpp
SchemaValidator validator;
validator
    .requireTable("users")
    .requireColumn("users", "id", "INTEGER")
    .requireColumn("users", "email", "TEXT")
    .requireNotNull("users", "email")
    .requireIndex("users", "idx_users_email");

auto errors = validator.validate(conn);
if (!errors.empty()) {
    for (const auto& err : errors) {
        std::cerr << err.message << "\n";
    }
}

// Or throw on failure
validator.validateOrThrow(conn);
```

### Repository Pattern

```cpp
struct User {
    int64_t id = 0;
    std::string name;
    std::string email;
    int age = 0;
};

class UserRepository : public Repository<User> {
public:
    UserRepository(Connection& conn) : Repository(conn, "users") {}

    int64_t create(const User& user) {
        auto stmt = conn_.prepare(
            "INSERT INTO users (name, email, age) VALUES (?, ?, ?)");
        stmt.bind(1, user.name)
            .bind(2, user.email)
            .bind(3, user.age)
            .execute();
        return conn_.lastInsertRowId();
    }

    std::vector<User> findByAge(int minAge) {
        auto stmt = conn_.prepare(
            "SELECT id, name, email, age FROM users WHERE age >= ?");
        stmt.bind(1, minAge);

        std::vector<User> results;
        while (stmt.step()) {
            results.push_back(fromRow(stmt));
        }
        return results;
    }

protected:
    User fromRow(Statement& stmt) override {
        return User{
            stmt.columnInt64(0),
            stmt.columnString(1),
            stmt.columnString(2),
            stmt.columnInt(3)
        };
    }
};

// Usage
UserRepository users(conn);
auto user = users.findById(1);
auto adults = users.findByAge(18);
```

### Query Builder

```cpp
auto results = QueryBuilder(conn, "users")
    .select(std::vector<std::string>{"name", "email", "age"})
    .where("age", ">", Value{int64_t(18)})
    .where("active", "=", Value{int64_t(1)})
    .orderBy("name")
    .limit(10)
    .fetchAll();

int64_t count = QueryBuilder(conn, "users")
    .where("active", "=", Value{int64_t(1)})
    .count();
```

### Batch Inserts

```cpp
BatchInsertBuilder batch(conn, "products", {"name", "price", "stock"});

for (const auto& product : products) {
    batch.addRow({
        Value{product.name},
        Value{product.price},
        Value{int64_t(product.stock)}
    });
}

int64_t inserted = batch.execute();  // Single transaction, much faster
```

### Error Handling

```cpp
try {
    conn.execute("INSERT INTO users (email) VALUES ('duplicate@test.com')");
} catch (const ConstraintException& e) {
    // Unique/foreign key violation
    std::cerr << "Constraint error: " << e.what() << "\n";
} catch (const QueryException& e) {
    // SQL syntax error, missing table, etc.
    std::cerr << "Query error: " << e.what() << "\n";
    std::cerr << "SQL: " << e.sql() << "\n";
} catch (const DatabaseException& e) {
    // Any database error (base class)
    std::cerr << "Database error: " << e.what() << "\n";
}
```

## Industry Best Practices Summary

| Practice | Why It Matters |
|----------|----------------|
| **RAII** | Guarantees cleanup even when exceptions are thrown |
| **Prepared Statements** | Prevents SQL injection (OWASP Top 10 vulnerability) |
| **Scoped Transactions** | Ensures atomicity; auto-rollback on failure |
| **Migrations** | Safe, versioned schema changes in production |
| **Schema Validation** | Catches configuration errors at startup |
| **Repository Pattern** | Testable code; clean architecture |
| **Batch Operations** | 10-100x performance improvement for bulk inserts |
| **Exception Hierarchy** | Granular error handling; rich context |

## Project Structure

```
sqlite3db/
├── include/sqlite3db/
│   ├── sqlite3db.hpp      # Main include header
│   ├── exceptions.hpp     # Exception hierarchy
│   ├── connection.hpp     # Connection management
│   ├── statement.hpp      # Prepared statements
│   ├── transaction.hpp    # Transactions & savepoints
│   ├── migration.hpp      # Schema migrations
│   └── repository.hpp     # Repository & query builder
├── src/                   # Implementation files
├── examples/main.cpp      # Comprehensive demo
├── tests/test_main.cpp    # Unit tests
├── CMakeLists.txt
└── Makefile
```

## License

MIT License
