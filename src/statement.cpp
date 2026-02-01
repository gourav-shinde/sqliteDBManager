/**
 * @file statement.cpp
 * @brief Implementation of Statement class
 */

#include "sqlite3db/statement.hpp"
#include "sqlite3db/connection.hpp"
#include <cstring>

namespace sqlite3db {

Statement::Statement(Connection& conn, const std::string& sql)
    : conn_(&conn)
    , sql_(sql)
{
    int result = sqlite3_prepare_v2(
        conn.handle(),
        sql.c_str(),
        static_cast<int>(sql.size()),
        &stmt_,
        nullptr
    );

    if (result != SQLITE_OK) {
        throw QueryException(sqlite3_errmsg(conn.handle()), sql, result);
    }
}

Statement::~Statement() {
    finalize();
}

Statement::Statement(Statement&& other) noexcept
    : stmt_(other.stmt_)
    , conn_(other.conn_)
    , sql_(std::move(other.sql_))
{
    other.stmt_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        finalize();
        stmt_ = other.stmt_;
        conn_ = other.conn_;
        sql_ = std::move(other.sql_);
        other.stmt_ = nullptr;
    }
    return *this;
}

void Statement::finalize() {
    if (stmt_) {
        sqlite3_finalize(stmt_);
        stmt_ = nullptr;
    }
}

void Statement::checkResult(int result, const std::string& operation) {
    if (result != SQLITE_OK) {
        throw QueryException(
            operation + " failed: " + sqlite3_errmsg(conn_->handle()),
            sql_,
            result
        );
    }
}

// ========== Parameter Binding ==========

Statement& Statement::bind(int index, int value) {
    checkResult(sqlite3_bind_int(stmt_, index, value), "bind int");
    return *this;
}

Statement& Statement::bind(int index, int64_t value) {
    checkResult(sqlite3_bind_int64(stmt_, index, value), "bind int64");
    return *this;
}

Statement& Statement::bind(int index, double value) {
    checkResult(sqlite3_bind_double(stmt_, index, value), "bind double");
    return *this;
}

Statement& Statement::bind(int index, const std::string& value) {
    // SQLITE_TRANSIENT tells SQLite to make its own copy
    // This is safer as we don't need to worry about string lifetime
    checkResult(
        sqlite3_bind_text(stmt_, index, value.c_str(),
                         static_cast<int>(value.size()), SQLITE_TRANSIENT),
        "bind text"
    );
    return *this;
}

Statement& Statement::bind(int index, const char* value) {
    if (value == nullptr) {
        return bind(index, null);
    }
    checkResult(
        sqlite3_bind_text(stmt_, index, value,
                         static_cast<int>(std::strlen(value)), SQLITE_TRANSIENT),
        "bind text"
    );
    return *this;
}

Statement& Statement::bind(int index, const std::vector<uint8_t>& value) {
    checkResult(
        sqlite3_bind_blob(stmt_, index, value.data(),
                         static_cast<int>(value.size()), SQLITE_TRANSIENT),
        "bind blob"
    );
    return *this;
}

Statement& Statement::bind(int index, NullValue) {
    checkResult(sqlite3_bind_null(stmt_, index), "bind null");
    return *this;
}

Statement& Statement::bind(int index, const Value& value) {
    std::visit([this, index](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, NullValue>) {
            bind(index, null);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            bind(index, v);
        } else if constexpr (std::is_same_v<T, double>) {
            bind(index, v);
        } else if constexpr (std::is_same_v<T, std::string>) {
            bind(index, v);
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            bind(index, v);
        }
    }, value);
    return *this;
}

Statement& Statement::clearBindings() {
    checkResult(sqlite3_clear_bindings(stmt_), "clear bindings");
    return *this;
}

// ========== Execution ==========

void Statement::execute() {
    int result = sqlite3_step(stmt_);

    if (result != SQLITE_DONE && result != SQLITE_ROW) {
        // Check for constraint violations
        if (result == SQLITE_CONSTRAINT) {
            throw ConstraintException(sqlite3_errmsg(conn_->handle()), result);
        }
        throw QueryException(sqlite3_errmsg(conn_->handle()), sql_, result);
    }

    // Reset for potential reuse
    sqlite3_reset(stmt_);
}

bool Statement::step() {
    int result = sqlite3_step(stmt_);

    if (result == SQLITE_ROW) {
        return true;
    } else if (result == SQLITE_DONE) {
        return false;
    } else {
        throw QueryException(sqlite3_errmsg(conn_->handle()), sql_, result);
    }
}

Statement& Statement::reset() {
    checkResult(sqlite3_reset(stmt_), "reset");
    return *this;
}

// ========== Column Access ==========

int Statement::columnCount() const {
    return sqlite3_column_count(stmt_);
}

std::string Statement::columnName(int index) const {
    const char* name = sqlite3_column_name(stmt_, index);
    return name ? name : "";
}

int Statement::columnType(int index) const {
    return sqlite3_column_type(stmt_, index);
}

bool Statement::isNull(int index) const {
    return sqlite3_column_type(stmt_, index) == SQLITE_NULL;
}

int Statement::columnInt(int index) const {
    return sqlite3_column_int(stmt_, index);
}

int64_t Statement::columnInt64(int index) const {
    return sqlite3_column_int64(stmt_, index);
}

double Statement::columnDouble(int index) const {
    return sqlite3_column_double(stmt_, index);
}

std::string Statement::columnString(int index) const {
    const unsigned char* text = sqlite3_column_text(stmt_, index);
    int size = sqlite3_column_bytes(stmt_, index);
    if (text == nullptr) {
        return "";
    }
    return std::string(reinterpret_cast<const char*>(text), size);
}

std::vector<uint8_t> Statement::columnBlob(int index) const {
    const void* data = sqlite3_column_blob(stmt_, index);
    int size = sqlite3_column_bytes(stmt_, index);
    if (data == nullptr || size == 0) {
        return {};
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    return std::vector<uint8_t>(bytes, bytes + size);
}

Value Statement::columnValue(int index) const {
    int type = sqlite3_column_type(stmt_, index);

    switch (type) {
        case SQLITE_NULL:
            return NullValue{};
        case SQLITE_INTEGER:
            return columnInt64(index);
        case SQLITE_FLOAT:
            return columnDouble(index);
        case SQLITE_TEXT:
            return columnString(index);
        case SQLITE_BLOB:
            return columnBlob(index);
        default:
            return NullValue{};
    }
}

std::optional<int64_t> Statement::columnOptionalInt64(int index) const {
    if (isNull(index)) {
        return std::nullopt;
    }
    return columnInt64(index);
}

std::optional<double> Statement::columnOptionalDouble(int index) const {
    if (isNull(index)) {
        return std::nullopt;
    }
    return columnDouble(index);
}

std::optional<std::string> Statement::columnOptionalString(int index) const {
    if (isNull(index)) {
        return std::nullopt;
    }
    return columnString(index);
}

} // namespace sqlite3db
