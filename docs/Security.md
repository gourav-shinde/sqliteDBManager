# Security Guide

This document covers security best practices for configuring and using the SQLite3DB library, including secure database configuration and schema authentication.

## Table of Contents

1. [Secure Database Configuration](#secure-database-configuration)
   - [ConnectionOptions Reference](#connectionoptions-reference)
   - [SQLite Runtime Security Options](#sqlite-runtime-security-options-critical) (load_extension, defensive, DQS)
2. [Schema Authentication](#schema-authentication)
3. [SQL Injection Prevention](#sql-injection-prevention)
4. [Additional Security Recommendations](#additional-security-recommendations)
5. [Security Checklist](#security-checklist)

---

## Secure Database Configuration

The `ConnectionOptions` struct provides several security-relevant settings. Here are the recommended configurations for production environments:

### ConnectionOptions Reference

```cpp
struct ConnectionOptions {
    bool enableWAL = true;              // Write-Ahead Logging
    int busyTimeoutMs = 5000;           // Lock timeout in milliseconds
    bool enableForeignKeys = true;      // Enforce referential integrity
    bool readOnly = false;              // Open in read-only mode
    bool createIfNotExists = true;      // Auto-create database file
    bool extendedResultCodes = true;    // Detailed SQLite error codes
};
```

### Recommended Security Settings

#### 1. Enable Foreign Keys (Critical)

```cpp
ConnectionOptions opts;
opts.enableForeignKeys = true;  // Default: true
```

**Why:** Foreign key constraints enforce referential integrity, preventing orphaned records and maintaining data consistency. Always keep this enabled unless you have a specific reason not to.

**Security Impact:** Prevents data corruption and ensures related records are properly validated.

#### 2. Use Read-Only Mode When Appropriate

```cpp
ConnectionOptions opts;
opts.readOnly = true;  // Enable for read-only operations
```

**When to use:**
- Reporting or analytics connections
- Backup verification processes
- Any process that should never modify data
- Web-facing query endpoints

**Security Impact:** Provides defense-in-depth by preventing accidental or malicious modifications even if application logic has vulnerabilities.

#### 3. Set Appropriate Busy Timeout

```cpp
ConnectionOptions opts;
opts.busyTimeoutMs = 5000;  // 5 seconds (default)
```

**Recommendations:**
- **Production web apps:** 1000-5000ms (fail fast, don't hang)
- **Background jobs:** 10000-30000ms (can wait longer)
- **Interactive CLI tools:** 500-2000ms (responsive to user)

**Security Impact:** Prevents denial-of-service conditions where locks cause indefinite hangs. Always set a reasonable timeout to ensure connections fail gracefully under contention.

#### 4. Control Database Creation

```cpp
ConnectionOptions opts;
opts.createIfNotExists = false;  // Fail if database doesn't exist
```

**When to disable:**
- Production environments where database should already exist
- Preventing accidental creation of databases from typos in paths
- When database must be provisioned through controlled processes

**Security Impact:** Prevents creation of unintended database files that could be exploited.

#### 5. Enable WAL Mode for Concurrency

```cpp
ConnectionOptions opts;
opts.enableWAL = true;  // Default: true
```

**Security Impact:** WAL mode provides better crash recovery and reduces corruption risk. It also enables multiple readers with a single writer, reducing lock contention.

### SQLite Runtime Security Options (Critical)

These options are configured via `sqlite3_db_config()` using the raw connection handle. They provide critical hardening for production environments.

#### 6. Disable Extension Loading (Critical)

```cpp
Connection conn("production.db", opts);

// Disable extension loading - CRITICAL for security
int result;
sqlite3_db_config(conn.handle(), SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 0, &result);
```

**Why:** SQLite can load external shared libraries (.so/.dll) as extensions. If an attacker can execute arbitrary SQL (even through SQL injection), they could load malicious code:

```sql
-- An attacker could execute this if load_extension is enabled:
SELECT load_extension('/path/to/malicious.so');
```

**Security Impact:** Prevents arbitrary code execution through SQLite. This is one of the most critical security settings. **Always disable in production unless you specifically need extensions.**

**Note:** This is different from `sqlite3_enable_load_extension()` which is a compile-time/global setting. `SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION` is per-connection.

#### 7. Enable Defensive Mode (Critical)

```cpp
Connection conn("production.db", opts);

// Enable defensive mode
int result;
sqlite3_db_config(conn.handle(), SQLITE_DBCONFIG_DEFENSIVE, 1, &result);
```

**Why:** Defensive mode (available since SQLite 3.26.0) prevents several dangerous operations:

| Blocked Operation | Attack Prevented |
|-------------------|------------------|
| Writing to shadow tables | Corrupting FTS/R-Tree indexes |
| Direct writes to `sqlite_master` | Schema corruption attacks |
| Using `PRAGMA writable_schema=ON` | Bypassing schema protections |
| Corrupting database via ATTACH | Cross-database attacks |

**Security Impact:** Prevents database corruption attacks even if an attacker has write access. Essential for multi-tenant applications or when processing untrusted data.

#### 8. Disable Double-Quoted Strings (Recommended)

```cpp
Connection conn("production.db", opts);

// Disable DQS for both DDL and DML
int result;
sqlite3_db_config(conn.handle(), SQLITE_DBCONFIG_DQS_DDL, 0, &result);
sqlite3_db_config(conn.handle(), SQLITE_DBCONFIG_DQS_DML, 0, &result);
```

**Why:** By default, SQLite allows double-quoted strings to be treated as string literals if they don't match any identifier. This can mask errors and create security issues:

```sql
-- With DQS enabled, this silently works even if 'username' column doesn't exist:
SELECT * FROM users WHERE "username" = "admin";
-- (Both "username" and "admin" treated as strings, always returns rows where 'admin' = 'admin')
```

**Security Impact:** Prevents silent failures that could lead to authentication bypasses or data leakage.

#### 9. Enable Trusted Schema (When Appropriate)

```cpp
Connection conn("production.db", opts);

// Disable trusted schema for extra security
int result;
sqlite3_db_config(conn.handle(), SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, &result);
```

**Why:** When disabled, SQL functions and virtual tables defined in the schema are treated as potentially malicious. This provides extra protection when:
- Opening databases from untrusted sources
- The database file could have been tampered with

**Trade-off:** May break functionality if your schema uses custom SQL functions or virtual tables. Test thoroughly before enabling.

### Complete Security Hardening Function

```cpp
#include <sqlite3db/sqlite3db.hpp>
#include <sqlite3.h>
#include <stdexcept>

using namespace sqlite3db;

void hardenConnection(Connection& conn) {
    int result;

    // 1. Disable extension loading (CRITICAL)
    if (sqlite3_db_config(conn.handle(),
            SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 0, &result) != SQLITE_OK) {
        throw std::runtime_error("Failed to disable load_extension");
    }

    // 2. Enable defensive mode (CRITICAL)
    if (sqlite3_db_config(conn.handle(),
            SQLITE_DBCONFIG_DEFENSIVE, 1, &result) != SQLITE_OK) {
        throw std::runtime_error("Failed to enable defensive mode");
    }

    // 3. Disable double-quoted strings (Recommended)
    sqlite3_db_config(conn.handle(), SQLITE_DBCONFIG_DQS_DDL, 0, &result);
    sqlite3_db_config(conn.handle(), SQLITE_DBCONFIG_DQS_DML, 0, &result);

    // 4. Disable trusted schema if opening untrusted databases
    // sqlite3_db_config(conn.handle(), SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, &result);
}
```

### Production Configuration Example

```cpp
#include <sqlite3db/sqlite3db.hpp>
using namespace sqlite3db;

// High-security production configuration
ConnectionOptions getProductionConfig() {
    ConnectionOptions opts;
    opts.enableWAL = true;              // Better crash recovery
    opts.busyTimeoutMs = 3000;          // Fail reasonably fast
    opts.enableForeignKeys = true;      // Enforce data integrity
    opts.readOnly = false;              // Set true for read replicas
    opts.createIfNotExists = false;     // Fail if DB missing
    opts.extendedResultCodes = true;    // Detailed error diagnostics
    return opts;
}

// Read-only connection for queries
ConnectionOptions getReadOnlyConfig() {
    ConnectionOptions opts = getProductionConfig();
    opts.readOnly = true;
    return opts;
}

// Full production setup with hardening
Connection createSecureConnection(const std::string& dbPath, bool readOnly = false) {
    auto opts = readOnly ? getReadOnlyConfig() : getProductionConfig();
    Connection conn(dbPath, opts);
    hardenConnection(conn);  // Apply runtime security settings
    return conn;
}
```

---

## Schema Authentication

Schema authentication ensures that your database schema matches the expected structure before your application processes any data. This prevents attacks or bugs where a modified schema could lead to data leakage, corruption, or application crashes.

### Using SchemaValidator

The `SchemaValidator` class provides a declarative way to verify schema integrity:

```cpp
#include <sqlite3db/sqlite3db.hpp>
using namespace sqlite3db;

bool authenticateSchema(Connection& conn) {
    SchemaValidator validator;

    // Define expected schema structure
    validator
        .requireTable("users")
        .requireColumn("users", "id", "INTEGER")
        .requireColumn("users", "email", "TEXT")
        .requireColumn("users", "password_hash", "TEXT")
        .requireNotNull("users", "email")
        .requireNotNull("users", "password_hash")
        .requireIndex("users", "idx_users_email")

        .requireTable("sessions")
        .requireColumn("sessions", "id", "TEXT")
        .requireColumn("sessions", "user_id", "INTEGER")
        .requireColumn("sessions", "expires_at", "INTEGER")
        .requireNotNull("sessions", "user_id");

    // Validate and get errors
    auto errors = validator.validate(conn);

    if (!errors.empty()) {
        for (const auto& error : errors) {
            std::cerr << "Schema error: " << error.message << std::endl;
        }
        return false;
    }

    return true;
}
```

### Schema Authentication Workflow

Follow this workflow to authenticate schemas in your application:

#### Step 1: Define Schema Requirements

Create a schema specification file or function that documents your expected schema:

```cpp
// schema_requirements.hpp
#pragma once
#include <sqlite3db/sqlite3db.hpp>

namespace app::schema {

inline sqlite3db::SchemaValidator createValidator() {
    sqlite3db::SchemaValidator validator;

    // Core user table
    validator
        .requireTable("users")
        .requireColumn("users", "id", "INTEGER")
        .requireColumn("users", "email", "TEXT")
        .requireColumn("users", "password_hash", "TEXT")
        .requireColumn("users", "created_at", "INTEGER")
        .requireNotNull("users", "email")
        .requireNotNull("users", "password_hash")
        .requireIndex("users", "idx_users_email");

    // Add more tables as needed...

    return validator;
}

} // namespace app::schema
```

#### Step 2: Validate on Application Startup

Always validate the schema before processing any data:

```cpp
#include "schema_requirements.hpp"

int main() {
    try {
        auto opts = getProductionConfig();
        Connection conn("production.db", opts);

        // Authenticate schema BEFORE any operations
        auto validator = app::schema::createValidator();
        validator.validateOrThrow(conn);  // Throws SchemaException on failure

        // Schema is valid - proceed with application
        runApplication(conn);

    } catch (const SchemaException& e) {
        std::cerr << "FATAL: Schema authentication failed: "
                  << e.what() << std::endl;
        return 1;
    } catch (const DatabaseException& e) {
        std::cerr << "Database error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
```

#### Step 3: Use Migration System for Schema Changes

Never modify the schema manually. Use the migration system to ensure changes are tracked and reproducible:

```cpp
MigrationManager migrations;

// Version 1: Initial schema
migrations.add(1, "Create users table", [](Connection& conn) {
    conn.execute(R"(
        CREATE TABLE users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            email TEXT NOT NULL UNIQUE,
            password_hash TEXT NOT NULL,
            created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
        )
    )");
    conn.execute("CREATE INDEX idx_users_email ON users(email)");
});

// Version 2: Add sessions table
migrations.add(2, "Create sessions table", [](Connection& conn) {
    conn.execute(R"(
        CREATE TABLE sessions (
            id TEXT PRIMARY KEY,
            user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            expires_at INTEGER NOT NULL,
            created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
        )
    )");
});

// Apply migrations
migrations.apply(conn);

// Then validate
auto validator = app::schema::createValidator();
validator.validateOrThrow(conn);
```

### Schema Hash Verification (Advanced)

For additional security, you can compute and verify a hash of your schema:

```cpp
#include <openssl/sha.h>  // Or any SHA-256 implementation
#include <sstream>
#include <iomanip>

std::string computeSchemaHash(Connection& conn) {
    // Get all table definitions
    Statement stmt = conn.prepare(
        "SELECT sql FROM sqlite_master "
        "WHERE type IN ('table', 'index') AND sql IS NOT NULL "
        "ORDER BY name"
    );

    std::stringstream schema;
    while (stmt.step()) {
        schema << stmt.columnString(0) << "\n";
    }

    // Compute SHA-256 hash
    std::string schemaStr = schema.str();
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(schemaStr.c_str()),
           schemaStr.length(), hash);

    // Convert to hex string
    std::stringstream hexHash;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        hexHash << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(hash[i]);
    }

    return hexHash.str();
}

bool verifySchemaHash(Connection& conn, const std::string& expectedHash) {
    std::string actualHash = computeSchemaHash(conn);

    // Constant-time comparison to prevent timing attacks
    if (actualHash.length() != expectedHash.length()) {
        return false;
    }

    volatile int result = 0;
    for (size_t i = 0; i < actualHash.length(); i++) {
        result |= actualHash[i] ^ expectedHash[i];
    }

    return result == 0;
}
```

**Usage:**

```cpp
// Store expected hash in configuration or code
const std::string EXPECTED_SCHEMA_HASH =
    "a1b2c3d4e5f6...";  // Your schema hash

if (!verifySchemaHash(conn, EXPECTED_SCHEMA_HASH)) {
    throw std::runtime_error("Schema hash mismatch - possible tampering");
}
```

---

## SQL Injection Prevention

The library uses prepared statements to prevent SQL injection. Always use parameter binding:

### Correct Usage (Safe)

```cpp
// Named parameters
Statement stmt = conn.prepare(
    "SELECT * FROM users WHERE email = :email AND active = :active"
);
stmt.bind(":email", userInput);
stmt.bind(":active", 1);

// Positional parameters
Statement stmt = conn.prepare(
    "SELECT * FROM users WHERE email = ? AND active = ?"
);
stmt.bind(1, userInput);
stmt.bind(2, 1);
```

### Incorrect Usage (Vulnerable - Never Do This)

```cpp
// NEVER concatenate user input into SQL strings
std::string sql = "SELECT * FROM users WHERE email = '" + userInput + "'";
conn.execute(sql);  // SQL INJECTION VULNERABILITY!
```

---

## Additional Security Recommendations

### 1. File Permissions

Ensure the database file has appropriate permissions:

```bash
# Owner read/write only
chmod 600 production.db

# Or owner read/write, group read
chmod 640 production.db
```

### 2. Database File Location

- Store database files outside the web root
- Use absolute paths to prevent path traversal
- Consider encrypted storage for sensitive data

### 3. Backup Security

- Encrypt backups at rest
- Verify backup integrity before restoration
- Test backup restoration procedures regularly

### 4. Connection Lifecycle

Always use RAII for proper resource cleanup:

```cpp
{
    Connection conn("database.db", opts);
    // Work with connection
}  // Connection automatically closed here
```

### 5. Transaction Isolation

Use appropriate transaction types based on your needs:

```cpp
// For read-heavy workloads
Transaction txn(conn, TransactionType::Deferred);

// When you need immediate write lock
Transaction txn(conn, TransactionType::Immediate);

// For exclusive access
Transaction txn(conn, TransactionType::Exclusive);
```

### 6. Error Handling

Never expose raw database errors to end users:

```cpp
try {
    // Database operations
} catch (const ConstraintException& e) {
    // Log full error internally
    logger.error("Constraint violation: {}", e.what());
    // Return sanitized error to user
    return UserError("Invalid data provided");
} catch (const DatabaseException& e) {
    logger.error("Database error: {}", e.what());
    return UserError("An error occurred. Please try again.");
}
```

---

## Security Checklist

Before deploying to production, verify:

### Connection Configuration
- [ ] `enableForeignKeys` is `true`
- [ ] `busyTimeoutMs` is set to a reasonable value (1000-5000ms)
- [ ] Read-only connections use `readOnly = true`
- [ ] `createIfNotExists` is `false` for production

### Runtime Security (SQLITE_DBCONFIG)
- [ ] `SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION` is disabled (set to 0)
- [ ] `SQLITE_DBCONFIG_DEFENSIVE` is enabled (set to 1)
- [ ] `SQLITE_DBCONFIG_DQS_DDL` is disabled (set to 0)
- [ ] `SQLITE_DBCONFIG_DQS_DML` is disabled (set to 0)
- [ ] `SQLITE_DBCONFIG_TRUSTED_SCHEMA` is disabled for untrusted databases

### Application Security
- [ ] Schema validation runs on startup
- [ ] All user inputs use prepared statements with parameter binding
- [ ] Database file permissions are restricted (chmod 600 or 640)
- [ ] Database file is stored outside web root
- [ ] Error messages don't expose internal details to users
- [ ] Backups are encrypted and tested regularly
