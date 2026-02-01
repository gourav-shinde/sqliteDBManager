/**
 * @file test_main.cpp
 * @brief Unit tests for sqlite3db library
 *
 * INDUSTRY PRACTICE #18: Test with In-Memory Databases
 * =====================================================
 * Using in-memory databases for testing provides:
 * - Speed: No disk I/O
 * - Isolation: Each test gets a fresh database
 * - Cleanup: Database automatically destroyed
 *
 * In production, you'd use a testing framework like Google Test,
 * Catch2, or doctest. This is a simple demonstration.
 */

#include <iostream>
#include <cassert>
#include <sstream>
#include "sqlite3db/sqlite3db.hpp"

using namespace sqlite3db;

// Simple test framework macros
#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "  Running " #name "... "; \
    try { \
        test_##name(); \
        std::cout << "PASSED\n"; \
        passed++; \
    } catch (const std::exception& e) { \
        std::cout << "FAILED: " << e.what() << "\n"; \
        failed++; \
    } \
} while(0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) throw std::runtime_error("Assertion failed: " #expr); \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::ostringstream oss; \
        oss << "Assertion failed: " << (a) << " != " << (b); \
        throw std::runtime_error(oss.str()); \
    } \
} while(0)

#define ASSERT_THROWS(expr, ExceptionType) do { \
    bool caught = false; \
    try { expr; } catch (const ExceptionType&) { caught = true; } \
    if (!caught) throw std::runtime_error("Expected " #ExceptionType " not thrown"); \
} while(0)

// ========== Connection Tests ==========

TEST(connection_open_memory) {
    auto conn = Connection::inMemory();
    ASSERT_TRUE(conn->isOpen());
    ASSERT_EQ(conn->path(), ":memory:");
}

TEST(connection_execute_basic) {
    auto conn = Connection::inMemory();
    conn->execute("CREATE TABLE test (id INTEGER PRIMARY KEY, value TEXT)");
    ASSERT_TRUE(conn->tableExists("test"));
}

TEST(connection_options) {
    ConnectionOptions opts;
    opts.enableWAL = true;
    opts.enableForeignKeys = true;

    auto conn = Connection::inMemory(opts);
    ASSERT_TRUE(conn->isOpen());
}

// ========== Statement Tests ==========

TEST(statement_bind_and_execute) {
    auto conn = Connection::inMemory();
    conn->execute("CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT)");

    auto stmt = conn->prepare("INSERT INTO test (name) VALUES (?)");
    stmt.bind(1, "Hello").execute();

    ASSERT_EQ(conn->lastInsertRowId(), 1);
}

TEST(statement_query_results) {
    auto conn = Connection::inMemory();
    conn->execute("CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT, value REAL)");
    conn->execute("INSERT INTO test (name, value) VALUES ('test', 3.14)");

    auto stmt = conn->prepare("SELECT * FROM test WHERE id = ?");
    stmt.bind(1, 1);

    ASSERT_TRUE(stmt.step());
    ASSERT_EQ(stmt.columnInt64(0), 1);
    ASSERT_EQ(stmt.columnString(1), "test");
    ASSERT_TRUE(stmt.columnDouble(2) > 3.13 && stmt.columnDouble(2) < 3.15);
}

TEST(statement_null_handling) {
    auto conn = Connection::inMemory();
    conn->execute("CREATE TABLE test (id INTEGER PRIMARY KEY, value TEXT)");

    auto stmt = conn->prepare("INSERT INTO test (value) VALUES (?)");
    stmt.bind(1, null).execute();

    stmt = conn->prepare("SELECT value FROM test WHERE id = 1");
    stmt.step();
    ASSERT_TRUE(stmt.isNull(0));
}

TEST(statement_named_parameters) {
    auto conn = Connection::inMemory();
    conn->execute("CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)");

    auto stmt = conn->prepare("INSERT INTO test (name, age) VALUES (:name, :age)");
    stmt.bind(":name", "Alice").bind(":age", 30).execute();

    stmt = conn->prepare("SELECT name, age FROM test");
    stmt.step();
    ASSERT_EQ(stmt.columnString(0), "Alice");
    ASSERT_EQ(stmt.columnInt(1), 30);
}

// ========== Transaction Tests ==========

TEST(transaction_commit) {
    auto conn = Connection::inMemory();
    conn->execute("CREATE TABLE test (id INTEGER PRIMARY KEY)");

    {
        Transaction txn = conn->beginTransaction();
        conn->execute("INSERT INTO test DEFAULT VALUES");
        txn.commit();
    }

    auto stmt = conn->prepare("SELECT COUNT(*) FROM test");
    stmt.step();
    ASSERT_EQ(stmt.columnInt64(0), 1);
}

TEST(transaction_rollback) {
    auto conn = Connection::inMemory();
    conn->execute("CREATE TABLE test (id INTEGER PRIMARY KEY)");

    {
        Transaction txn = conn->beginTransaction();
        conn->execute("INSERT INTO test DEFAULT VALUES");
        // No commit - destructor will rollback
    }

    auto stmt = conn->prepare("SELECT COUNT(*) FROM test");
    stmt.step();
    ASSERT_EQ(stmt.columnInt64(0), 0);  // Row was rolled back
}

TEST(transaction_explicit_rollback) {
    auto conn = Connection::inMemory();
    conn->execute("CREATE TABLE test (id INTEGER PRIMARY KEY)");

    Transaction txn = conn->beginTransaction();
    conn->execute("INSERT INTO test DEFAULT VALUES");
    txn.rollback();

    auto stmt = conn->prepare("SELECT COUNT(*) FROM test");
    stmt.step();
    ASSERT_EQ(stmt.columnInt64(0), 0);
}

TEST(savepoint_commit) {
    auto conn = Connection::inMemory();
    conn->execute("CREATE TABLE test (id INTEGER PRIMARY KEY)");

    Transaction txn = conn->beginTransaction();

    conn->execute("INSERT INTO test DEFAULT VALUES");

    {
        auto sp = txn.savepoint("sp1");
        conn->execute("INSERT INTO test DEFAULT VALUES");
        sp.release();  // Commit savepoint
    }

    txn.commit();

    auto stmt = conn->prepare("SELECT COUNT(*) FROM test");
    stmt.step();
    ASSERT_EQ(stmt.columnInt64(0), 2);
}

TEST(savepoint_rollback) {
    auto conn = Connection::inMemory();
    conn->execute("CREATE TABLE test (id INTEGER PRIMARY KEY)");

    Transaction txn = conn->beginTransaction();

    conn->execute("INSERT INTO test DEFAULT VALUES");

    {
        auto sp = txn.savepoint("sp1");
        conn->execute("INSERT INTO test DEFAULT VALUES");
        sp.rollback();  // Rollback just this part
    }

    txn.commit();

    auto stmt = conn->prepare("SELECT COUNT(*) FROM test");
    stmt.step();
    ASSERT_EQ(stmt.columnInt64(0), 1);  // Only first insert kept
}

// ========== Migration Tests ==========

TEST(migration_apply) {
    auto conn = Connection::inMemory();

    MigrationManager migrations;
    migrations.add(1, "Create test table", [](Connection& db) {
        db.execute("CREATE TABLE test (id INTEGER PRIMARY KEY)");
    });
    migrations.add(2, "Add column", [](Connection& db) {
        db.execute("ALTER TABLE test ADD COLUMN name TEXT");
    });

    ASSERT_EQ(migrations.currentVersion(*conn), 0);
    migrations.apply(*conn);
    ASSERT_EQ(migrations.currentVersion(*conn), 2);
    ASSERT_TRUE(conn->tableExists("test"));
}

TEST(migration_partial) {
    auto conn = Connection::inMemory();

    MigrationManager migrations;
    migrations.add(1, "v1", [](Connection& db) {
        db.execute("CREATE TABLE v1 (id INTEGER)");
    });
    migrations.add(2, "v2", [](Connection& db) {
        db.execute("CREATE TABLE v2 (id INTEGER)");
    });
    migrations.add(3, "v3", [](Connection& db) {
        db.execute("CREATE TABLE v3 (id INTEGER)");
    });

    migrations.applyTo(*conn, 2);
    ASSERT_EQ(migrations.currentVersion(*conn), 2);
    ASSERT_TRUE(conn->tableExists("v1"));
    ASSERT_TRUE(conn->tableExists("v2"));
    ASSERT_TRUE(!conn->tableExists("v3"));
}

// ========== Schema Validator Tests ==========

TEST(schema_validator_pass) {
    auto conn = Connection::inMemory();
    conn->execute("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT NOT NULL)");
    conn->execute("CREATE INDEX idx_users_name ON users(name)");

    SchemaValidator validator;
    validator.requireTable("users")
             .requireColumn("users", "id", "INTEGER")
             .requireColumn("users", "name", "TEXT")
             .requireNotNull("users", "name")
             .requireIndex("users", "idx_users_name");

    auto errors = validator.validate(*conn);
    ASSERT_TRUE(errors.empty());
}

TEST(schema_validator_fail) {
    auto conn = Connection::inMemory();
    conn->execute("CREATE TABLE users (id INTEGER)");

    SchemaValidator validator;
    validator.requireTable("users")
             .requireTable("posts")  // Missing
             .requireColumn("users", "name", "TEXT");  // Missing

    auto errors = validator.validate(*conn);
    ASSERT_EQ(errors.size(), 2);
}

// ========== Query Builder Tests ==========

TEST(query_builder_select) {
    auto conn = Connection::inMemory();
    conn->execute("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)");
    conn->execute("INSERT INTO users (name, age) VALUES ('Alice', 30)");
    conn->execute("INSERT INTO users (name, age) VALUES ('Bob', 25)");

    auto results = QueryBuilder(*conn, "users")
        .select(std::vector<std::string>{"name", "age"})
        .where("age", ">", Value{int64_t(20)})
        .orderBy("name")
        .fetchAll();

    ASSERT_EQ(results.size(), 2);
    ASSERT_EQ(std::get<std::string>(results[0][0]), "Alice");
    ASSERT_EQ(std::get<std::string>(results[1][0]), "Bob");
}

TEST(query_builder_count) {
    auto conn = Connection::inMemory();
    conn->execute("CREATE TABLE users (id INTEGER PRIMARY KEY, active INTEGER)");
    conn->execute("INSERT INTO users (active) VALUES (1)");
    conn->execute("INSERT INTO users (active) VALUES (1)");
    conn->execute("INSERT INTO users (active) VALUES (0)");

    int64_t count = QueryBuilder(*conn, "users")
        .where("active", "=", Value{int64_t(1)})
        .count();

    ASSERT_EQ(count, 2);
}

// ========== Batch Insert Tests ==========

TEST(batch_insert) {
    auto conn = Connection::inMemory();
    conn->execute("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, value INTEGER)");

    BatchInsertBuilder batch(*conn, "items", {"name", "value"});

    for (int i = 0; i < 100; ++i) {
        batch.addRow({Value{"item" + std::to_string(i)}, Value{int64_t(i * 10)}});
    }

    int64_t inserted = batch.execute();
    ASSERT_EQ(inserted, 100);

    auto stmt = conn->prepare("SELECT COUNT(*) FROM items");
    stmt.step();
    ASSERT_EQ(stmt.columnInt64(0), 100);
}

// ========== Exception Tests ==========

TEST(exception_query) {
    auto conn = Connection::inMemory();
    ASSERT_THROWS(conn->execute("SELECT * FROM nonexistent"), QueryException);
}

TEST(exception_constraint) {
    auto conn = Connection::inMemory();
    conn->execute("CREATE TABLE test (id INTEGER PRIMARY KEY, email TEXT UNIQUE)");
    conn->execute("INSERT INTO test (email) VALUES ('test@example.com')");

    ASSERT_THROWS(
        conn->execute("INSERT INTO test (email) VALUES ('test@example.com')"),
        ConstraintException
    );
}

// ========== Main ==========

int main() {
    int passed = 0;
    int failed = 0;

    std::cout << "\nRunning sqlite3db tests...\n\n";

    std::cout << "Connection tests:\n";
    RUN_TEST(connection_open_memory);
    RUN_TEST(connection_execute_basic);
    RUN_TEST(connection_options);

    std::cout << "\nStatement tests:\n";
    RUN_TEST(statement_bind_and_execute);
    RUN_TEST(statement_query_results);
    RUN_TEST(statement_null_handling);
    RUN_TEST(statement_named_parameters);

    std::cout << "\nTransaction tests:\n";
    RUN_TEST(transaction_commit);
    RUN_TEST(transaction_rollback);
    RUN_TEST(transaction_explicit_rollback);
    RUN_TEST(savepoint_commit);
    RUN_TEST(savepoint_rollback);

    std::cout << "\nMigration tests:\n";
    RUN_TEST(migration_apply);
    RUN_TEST(migration_partial);

    std::cout << "\nSchema validator tests:\n";
    RUN_TEST(schema_validator_pass);
    RUN_TEST(schema_validator_fail);

    std::cout << "\nQuery builder tests:\n";
    RUN_TEST(query_builder_select);
    RUN_TEST(query_builder_count);

    std::cout << "\nBatch insert tests:\n";
    RUN_TEST(batch_insert);

    std::cout << "\nException tests:\n";
    RUN_TEST(exception_query);
    RUN_TEST(exception_constraint);

    std::cout << "\n" << std::string(40, '=') << "\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << std::string(40, '=') << "\n";

    return failed > 0 ? 1 : 0;
}
