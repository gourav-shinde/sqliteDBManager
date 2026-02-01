/**
 * @file main.cpp
 * @brief Comprehensive example demonstrating all database best practices
 *
 * This example shows:
 * 1. Connection management with RAII
 * 2. Schema migrations
 * 3. Schema validation
 * 4. Prepared statements (SQL injection prevention)
 * 5. Transaction management
 * 6. Repository pattern
 * 7. Query builder
 * 8. Batch inserts
 * 9. Error handling
 */

#include <iostream>
#include <iomanip>
#include "sqlite3db/sqlite3db.hpp"

using namespace sqlite3db;

// ========== Domain Model ==========
// In real applications, these would be in separate header files

struct User {
    int64_t id = 0;
    std::string name;
    std::string email;
    int age = 0;
    bool active = true;
};

struct Post {
    int64_t id = 0;
    int64_t userId = 0;
    std::string title;
    std::string content;
};

// ========== Repository Implementation ==========
// Encapsulates all User data access

class UserRepository : public Repository<User> {
public:
    explicit UserRepository(Connection& conn)
        : Repository(conn, "users") {}

    // Create a new user
    int64_t create(const User& user) {
        auto stmt = conn_.prepare(R"(
            INSERT INTO users (name, email, age, active)
            VALUES (?, ?, ?, ?)
        )");
        stmt.bind(1, user.name)
            .bind(2, user.email)
            .bind(3, user.age)
            .bind(4, user.active ? 1 : 0)
            .execute();
        return conn_.lastInsertRowId();
    }

    // Update an existing user
    bool update(const User& user) {
        auto stmt = conn_.prepare(R"(
            UPDATE users SET name = ?, email = ?, age = ?, active = ?
            WHERE id = ?
        )");
        stmt.bind(1, user.name)
            .bind(2, user.email)
            .bind(3, user.age)
            .bind(4, user.active ? 1 : 0)
            .bind(5, user.id)
            .execute();
        return conn_.changes() > 0;
    }

    // Find users by criteria
    std::vector<User> findByAge(int minAge, int maxAge) {
        auto stmt = conn_.prepare(R"(
            SELECT id, name, email, age, active FROM users
            WHERE age BETWEEN ? AND ?
            ORDER BY name
        )");
        stmt.bind(1, minAge).bind(2, maxAge);

        std::vector<User> results;
        while (stmt.step()) {
            results.push_back(fromRow(stmt));
        }
        return results;
    }

    // Find active users
    std::vector<User> findActive() {
        auto stmt = conn_.prepare(R"(
            SELECT id, name, email, age, active FROM users
            WHERE active = 1 ORDER BY name
        )");

        std::vector<User> results;
        while (stmt.step()) {
            results.push_back(fromRow(stmt));
        }
        return results;
    }

    // Find user by email (unique lookup)
    std::optional<User> findByEmail(const std::string& email) {
        auto stmt = conn_.prepare(R"(
            SELECT id, name, email, age, active FROM users WHERE email = ?
        )");
        stmt.bind(1, email);

        if (stmt.step()) {
            return fromRow(stmt);
        }
        return std::nullopt;
    }

protected:
    User fromRow(Statement& stmt) override {
        User user;
        user.id = stmt.columnInt64(0);
        user.name = stmt.columnString(1);
        user.email = stmt.columnString(2);
        user.age = stmt.columnInt(3);
        user.active = stmt.columnInt(4) != 0;
        return user;
    }

    void bindForInsert(Statement& stmt, const User& user) override {
        stmt.bind(1, user.name)
            .bind(2, user.email)
            .bind(3, user.age)
            .bind(4, user.active ? 1 : 0);
    }
};

// ========== Migration Definitions ==========

MigrationManager createMigrations() {
    MigrationManager migrations;

    // Version 1: Initial schema
    migrations.add(1, "Create users table", [](Connection& db) {
        db.execute(R"(
            CREATE TABLE users (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                email TEXT NOT NULL UNIQUE,
                created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
            )
        )");
    });

    // Version 2: Add age column
    migrations.add(2, "Add age to users", [](Connection& db) {
        db.execute("ALTER TABLE users ADD COLUMN age INTEGER DEFAULT 0");
    });

    // Version 3: Add active flag
    migrations.add(3, "Add active flag to users", [](Connection& db) {
        db.execute("ALTER TABLE users ADD COLUMN active INTEGER DEFAULT 1");
    });

    // Version 4: Create posts table with foreign key
    migrations.add(4, "Create posts table", [](Connection& db) {
        db.execute(R"(
            CREATE TABLE posts (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                user_id INTEGER NOT NULL,
                title TEXT NOT NULL,
                content TEXT,
                created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
                FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
            )
        )");
    });

    // Version 5: Add index for performance
    migrations.add(5, "Add index on users email", [](Connection& db) {
        db.execute("CREATE INDEX IF NOT EXISTS idx_users_email ON users(email)");
    });

    return migrations;
}

// ========== Schema Validator ==========

SchemaValidator createValidator() {
    SchemaValidator validator;

    validator
        .requireTable("users")
        .requireColumn("users", "id", "INTEGER")
        .requireColumn("users", "name", "TEXT")
        .requireColumn("users", "email", "TEXT")
        .requireNotNull("users", "name")
        .requireNotNull("users", "email")
        .requireTable("posts")
        .requireColumn("posts", "user_id", "INTEGER")
        .requireIndex("users", "idx_users_email");

    return validator;
}

// ========== Demo Functions ==========

void printSection(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << " " << title << "\n";
    std::cout << std::string(60, '=') << "\n\n";
}

void demonstrateBasicOperations(Connection& conn) {
    printSection("Basic Operations with Prepared Statements");

    // CORRECT: Using prepared statements
    std::cout << "Using prepared statements (SAFE from SQL injection):\n";

    auto stmt = conn.prepare("INSERT INTO users (name, email, age) VALUES (?, ?, ?)");
    stmt.bind(1, "Alice")
        .bind(2, "alice@example.com")
        .bind(3, 30)
        .execute();

    std::cout << "  Inserted user with ID: " << conn.lastInsertRowId() << "\n";

    // Demonstrate statement reuse
    stmt.reset().clearBindings();
    stmt.bind(1, "Bob")
        .bind(2, "bob@example.com")
        .bind(3, 25)
        .execute();

    std::cout << "  Inserted user with ID: " << conn.lastInsertRowId() << "\n";

    // Query with prepared statement
    auto query = conn.prepare("SELECT id, name, email, age FROM users WHERE age > ?");
    query.bind(1, 20);

    std::cout << "\n  Users older than 20:\n";
    while (query.step()) {
        std::cout << "    ID: " << query.columnInt64(0)
                  << ", Name: " << query.columnString(1)
                  << ", Email: " << query.columnString(2)
                  << ", Age: " << query.columnInt(3) << "\n";
    }
}

void demonstrateTransactions(Connection& conn) {
    printSection("Transaction Management");

    std::cout << "Demonstrating atomic transaction:\n";

    try {
        // Transaction ensures atomicity
        Transaction txn = conn.beginTransaction();

        conn.execute("INSERT INTO users (name, email, age) VALUES ('Charlie', 'charlie@example.com', 35)");
        conn.execute("INSERT INTO users (name, email, age) VALUES ('Diana', 'diana@example.com', 28)");

        // Both inserts committed together
        txn.commit();
        std::cout << "  Transaction committed successfully\n";

    } catch (const DatabaseException& e) {
        std::cout << "  Transaction rolled back: " << e.what() << "\n";
    }

    // Demonstrate rollback
    std::cout << "\nDemonstrating automatic rollback on exception:\n";

    int64_t countBefore = 0;
    {
        auto stmt = conn.prepare("SELECT COUNT(*) FROM users");
        stmt.step();
        countBefore = stmt.columnInt64(0);
    }

    try {
        Transaction txn = conn.beginTransaction();

        conn.execute("INSERT INTO users (name, email, age) VALUES ('Eve', 'eve@example.com', 22)");

        // This will fail due to duplicate email (unique constraint)
        conn.execute("INSERT INTO users (name, email, age) VALUES ('Eve2', 'alice@example.com', 23)");

        txn.commit();  // Won't be reached

    } catch (const DatabaseException& e) {
        std::cout << "  Caught exception: " << e.what() << "\n";
        std::cout << "  Transaction automatically rolled back!\n";
    }

    int64_t countAfter = 0;
    {
        auto stmt = conn.prepare("SELECT COUNT(*) FROM users");
        stmt.step();
        countAfter = stmt.columnInt64(0);
    }

    std::cout << "  Users before: " << countBefore << ", after: " << countAfter << "\n";
    std::cout << "  (Both inserts were rolled back)\n";
}

void demonstrateSavepoints(Connection& conn) {
    printSection("Savepoints (Partial Rollback)");

    std::cout << "Demonstrating partial rollback with savepoints:\n";

    Transaction txn = conn.beginTransaction();

    conn.execute("INSERT INTO users (name, email, age) VALUES ('Frank', 'frank@example.com', 40)");
    std::cout << "  Inserted Frank\n";

    {
        // Savepoint for risky operation
        auto sp = txn.savepoint("risky_operation");

        conn.execute("INSERT INTO users (name, email, age) VALUES ('Grace', 'grace@example.com', 32)");
        std::cout << "  Inserted Grace (in savepoint)\n";

        // Decide to roll back just this part
        sp.rollback();
        std::cout << "  Rolled back savepoint (Grace removed)\n";
    }

    conn.execute("INSERT INTO users (name, email, age) VALUES ('Henry', 'henry@example.com', 45)");
    std::cout << "  Inserted Henry\n";

    txn.commit();
    std::cout << "  Transaction committed (Frank and Henry kept, Grace removed)\n";

    // Verify
    auto stmt = conn.prepare("SELECT name FROM users WHERE name IN ('Frank', 'Grace', 'Henry')");
    std::cout << "\n  Final users: ";
    while (stmt.step()) {
        std::cout << stmt.columnString(0) << " ";
    }
    std::cout << "\n";
}

void demonstrateRepository(Connection& conn) {
    printSection("Repository Pattern");

    UserRepository users(conn);

    std::cout << "Using repository for clean data access:\n\n";

    // Create
    User newUser;
    newUser.name = "Ivan";
    newUser.email = "ivan@example.com";
    newUser.age = 33;
    newUser.active = true;

    int64_t id = users.create(newUser);
    std::cout << "  Created user: " << newUser.name << " (ID: " << id << ")\n";

    // Read
    auto found = users.findById(id);
    if (found) {
        std::cout << "  Found by ID: " << found->name << ", " << found->email << "\n";
    }

    // Update
    found->age = 34;
    users.update(*found);
    std::cout << "  Updated age to 34\n";

    // Query
    auto activeUsers = users.findActive();
    std::cout << "\n  Active users (" << activeUsers.size() << "):\n";
    for (const auto& user : activeUsers) {
        std::cout << "    - " << user.name << " (" << user.email << "), age " << user.age << "\n";
    }

    // Count
    std::cout << "\n  Total users: " << users.count() << "\n";
}

void demonstrateQueryBuilder(Connection& conn) {
    printSection("Query Builder");

    std::cout << "Building queries fluently:\n\n";

    // Simple query
    auto results = QueryBuilder(conn, "users")
        .select(std::vector<std::string>{"name", "email", "age"})
        .where("age", ">", Value{int64_t(25)})
        .where("active", "=", Value{int64_t(1)})
        .orderBy("name")
        .limit(5)
        .fetchAll();

    std::cout << "  Users over 25 (active):\n";
    for (const auto& row : results) {
        std::cout << "    - " << std::get<std::string>(row[0])
                  << " (" << std::get<std::string>(row[1]) << ")"
                  << ", age " << std::get<int64_t>(row[2]) << "\n";
    }

    // Count query
    int64_t count = QueryBuilder(conn, "users")
        .where("age", ">=", Value{int64_t(30)})
        .count();

    std::cout << "\n  Users 30 or older: " << count << "\n";

    // Show generated SQL
    std::string sql = QueryBuilder(conn, "users")
        .select(std::vector<std::string>{"id", "name"})
        .where("active", "=", Value{int64_t(1)})
        .orderBy("created_at", false)
        .limit(10)
        .toSql();

    std::cout << "\n  Generated SQL: " << sql << "\n";
}

void demonstrateBatchInsert(Connection& conn) {
    printSection("Batch Insert (Performance)");

    std::cout << "Inserting multiple rows efficiently:\n\n";

    // Clear posts first
    conn.execute("DELETE FROM posts");

    // Get a user ID for foreign key
    auto stmt = conn.prepare("SELECT id FROM users LIMIT 1");
    stmt.step();
    int64_t userId = stmt.columnInt64(0);

    BatchInsertBuilder batch(conn, "posts", {"user_id", "title", "content"});

    // Add 100 rows
    for (int i = 1; i <= 100; ++i) {
        batch.addRow({
            Value{userId},
            Value{"Post #" + std::to_string(i)},
            Value{"Content for post " + std::to_string(i)}
        });
    }

    int64_t inserted = batch.execute();
    std::cout << "  Inserted " << inserted << " posts in a single batch transaction\n";

    // Verify
    stmt = conn.prepare("SELECT COUNT(*) FROM posts");
    stmt.step();
    std::cout << "  Total posts in database: " << stmt.columnInt64(0) << "\n";
}

void demonstrateErrorHandling(Connection& conn) {
    printSection("Error Handling");

    std::cout << "Demonstrating exception hierarchy:\n\n";

    // Constraint violation
    try {
        conn.execute("INSERT INTO users (name, email, age) VALUES ('Test', 'alice@example.com', 20)");
    } catch (const ConstraintException& e) {
        std::cout << "  ConstraintException caught (unique email):\n";
        std::cout << "    " << e.what() << "\n\n";
    }

    // Query error
    try {
        conn.execute("SELECT * FROM nonexistent_table");
    } catch (const QueryException& e) {
        std::cout << "  QueryException caught (bad table):\n";
        std::cout << "    Error code: " << e.errorCode() << "\n\n";
    }

    // Catching all database errors
    try {
        conn.execute("INVALID SQL SYNTAX HERE");
    } catch (const DatabaseException& e) {
        std::cout << "  DatabaseException caught (base class):\n";
        std::cout << "    " << e.what() << "\n";
    }
}

void demonstrateSchemaValidation(Connection& conn) {
    printSection("Schema Validation");

    std::cout << "Validating database schema:\n\n";

    auto validator = createValidator();
    auto errors = validator.validate(conn);

    if (errors.empty()) {
        std::cout << "  Schema validation passed!\n";
    } else {
        std::cout << "  Schema validation errors:\n";
        for (const auto& error : errors) {
            std::cout << "    [" << error.type << "] " << error.message << "\n";
        }
    }

    // Demonstrate validation failure
    std::cout << "\n  Testing validation with missing requirements:\n";

    SchemaValidator strictValidator;
    strictValidator
        .requireTable("users")
        .requireTable("nonexistent_table")  // This will fail
        .requireColumn("users", "id", "INTEGER")
        .requireColumn("users", "missing_column", "TEXT");  // This will fail

    errors = strictValidator.validate(conn);
    std::cout << "  Errors found: " << errors.size() << "\n";
    for (const auto& error : errors) {
        std::cout << "    - " << error.message << "\n";
    }
}

// ========== Main ==========

int main() {
    std::cout << "SQLite3DB Library - Industry Best Practices Demo\n";
    std::cout << "SQLite version: " << sqliteVersion() << "\n";
    std::cout << "Library version: " << VERSION_STRING << "\n";

    try {
        // Create an in-memory database for testing
        // In production, you'd use a file path
        ConnectionOptions options;
        options.enableWAL = true;
        options.enableForeignKeys = true;

        auto conn = Connection::inMemory(options);

        printSection("Schema Migrations");

        std::cout << "Applying migrations:\n";

        auto migrations = createMigrations();
        std::cout << "  Current version: " << migrations.currentVersion(*conn) << "\n";
        std::cout << "  Latest version: " << migrations.latestVersion() << "\n";

        auto pending = migrations.pending(*conn);
        std::cout << "  Pending migrations: " << pending.size() << "\n";

        for (const auto& m : pending) {
            std::cout << "    - v" << m.version << ": " << m.description << "\n";
        }

        migrations.apply(*conn);
        std::cout << "\n  Migrations applied. Current version: "
                  << migrations.currentVersion(*conn) << "\n";

        // Run demonstrations
        demonstrateBasicOperations(*conn);
        demonstrateTransactions(*conn);
        demonstrateSavepoints(*conn);
        demonstrateRepository(*conn);
        demonstrateQueryBuilder(*conn);
        demonstrateBatchInsert(*conn);
        demonstrateErrorHandling(*conn);
        demonstrateSchemaValidation(*conn);

        printSection("Summary");

        std::cout << "Key takeaways:\n";
        std::cout << "  1. Use RAII for automatic resource management\n";
        std::cout << "  2. ALWAYS use prepared statements (prevent SQL injection)\n";
        std::cout << "  3. Use transactions for atomic operations\n";
        std::cout << "  4. Use migrations for schema evolution\n";
        std::cout << "  5. Use the repository pattern for clean architecture\n";
        std::cout << "  6. Handle errors with a proper exception hierarchy\n";
        std::cout << "  7. Use batch inserts for bulk operations\n";
        std::cout << "  8. Validate schema at startup\n";

        std::cout << "\nDemo completed successfully!\n";

    } catch (const DatabaseException& e) {
        std::cerr << "Database error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
