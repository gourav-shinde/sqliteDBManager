/**
 * @file repository.cpp
 * @brief Implementation of QueryBuilder, InsertBuilder, and BatchInsertBuilder
 */

#include "sqlite3db/repository.hpp"
#include <sstream>

namespace sqlite3db {

// ========== QueryBuilder ==========

QueryBuilder::QueryBuilder(Connection& conn, const std::string& table)
    : conn_(conn)
    , table_(table)
{}

QueryBuilder& QueryBuilder::select(const std::vector<std::string>& columns) {
    std::ostringstream oss;
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << columns[i];
    }
    selectClause_ = oss.str();
    return *this;
}

QueryBuilder& QueryBuilder::select(const std::string& columns) {
    selectClause_ = columns;
    return *this;
}

QueryBuilder& QueryBuilder::selectAll() {
    selectClause_ = "*";
    return *this;
}

QueryBuilder& QueryBuilder::from(const std::string& table) {
    table_ = table;
    return *this;
}

QueryBuilder& QueryBuilder::where(const std::string& column, const std::string& op, const Value& value) {
    std::string clause = column + " " + op + " ?";
    whereClauses_.push_back(clause);
    whereValues_.push_back(value);
    return *this;
}

QueryBuilder& QueryBuilder::where(const std::string& rawCondition, const Value& value) {
    whereClauses_.push_back(rawCondition);
    whereValues_.push_back(value);
    return *this;
}

QueryBuilder& QueryBuilder::whereNull(const std::string& column) {
    whereClauses_.push_back(column + " IS NULL");
    return *this;
}

QueryBuilder& QueryBuilder::whereNotNull(const std::string& column) {
    whereClauses_.push_back(column + " IS NOT NULL");
    return *this;
}

QueryBuilder& QueryBuilder::whereIn(const std::string& column, const std::vector<Value>& values) {
    if (values.empty()) {
        // Empty IN clause - no results
        whereClauses_.push_back("1 = 0");
        return *this;
    }

    std::ostringstream oss;
    oss << column << " IN (";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << "?";
        whereValues_.push_back(values[i]);
    }
    oss << ")";
    whereClauses_.push_back(oss.str());
    return *this;
}

QueryBuilder& QueryBuilder::join(const std::string& table, const std::string& condition) {
    joins_.push_back("JOIN " + table + " ON " + condition);
    return *this;
}

QueryBuilder& QueryBuilder::leftJoin(const std::string& table, const std::string& condition) {
    joins_.push_back("LEFT JOIN " + table + " ON " + condition);
    return *this;
}

QueryBuilder& QueryBuilder::orderBy(const std::string& column, bool ascending) {
    orderByClause_ = column + (ascending ? " ASC" : " DESC");
    return *this;
}

QueryBuilder& QueryBuilder::limit(int count) {
    limit_ = count;
    return *this;
}

QueryBuilder& QueryBuilder::offset(int count) {
    offset_ = count;
    return *this;
}

QueryBuilder& QueryBuilder::groupBy(const std::string& column) {
    groupByClause_ = column;
    return *this;
}

QueryBuilder& QueryBuilder::having(const std::string& condition, const Value& value) {
    havingClause_ = condition;
    havingValue_ = value;
    hasHaving_ = true;
    return *this;
}

std::string QueryBuilder::toSql() const {
    std::ostringstream oss;

    oss << "SELECT " << selectClause_ << " FROM " << table_;

    // JOINs
    for (const auto& join : joins_) {
        oss << " " << join;
    }

    // WHERE
    if (!whereClauses_.empty()) {
        oss << " WHERE ";
        for (size_t i = 0; i < whereClauses_.size(); ++i) {
            if (i > 0) oss << " AND ";
            oss << whereClauses_[i];
        }
    }

    // GROUP BY
    if (!groupByClause_.empty()) {
        oss << " GROUP BY " << groupByClause_;
    }

    // HAVING
    if (hasHaving_) {
        oss << " HAVING " << havingClause_;
    }

    // ORDER BY
    if (!orderByClause_.empty()) {
        oss << " ORDER BY " << orderByClause_;
    }

    // LIMIT/OFFSET
    if (limit_ >= 0) {
        oss << " LIMIT " << limit_;
    }
    if (offset_ >= 0) {
        oss << " OFFSET " << offset_;
    }

    return oss.str();
}

std::vector<std::vector<Value>> QueryBuilder::fetchAll() {
    auto stmt = conn_.prepare(toSql());

    // Bind WHERE values
    int paramIndex = 1;
    for (const auto& value : whereValues_) {
        stmt.bind(paramIndex++, value);
    }

    // Bind HAVING value if present
    if (hasHaving_) {
        stmt.bind(paramIndex++, havingValue_);
    }

    std::vector<std::vector<Value>> results;
    int colCount = stmt.columnCount();

    while (stmt.step()) {
        std::vector<Value> row;
        row.reserve(colCount);
        for (int i = 0; i < colCount; ++i) {
            row.push_back(stmt.columnValue(i));
        }
        results.push_back(std::move(row));
    }

    return results;
}

std::optional<std::vector<Value>> QueryBuilder::fetchOne() {
    // Ensure we only get one row
    limit(1);

    auto results = fetchAll();
    if (results.empty()) {
        return std::nullopt;
    }
    return results[0];
}

int64_t QueryBuilder::count() {
    // Save current select clause
    std::string savedSelect = selectClause_;
    selectClause_ = "COUNT(*)";

    auto stmt = conn_.prepare(toSql());

    int paramIndex = 1;
    for (const auto& value : whereValues_) {
        stmt.bind(paramIndex++, value);
    }

    // Restore select clause
    selectClause_ = savedSelect;

    if (stmt.step()) {
        return stmt.columnInt64(0);
    }
    return 0;
}

// ========== InsertBuilder ==========

InsertBuilder::InsertBuilder(Connection& conn, const std::string& table)
    : conn_(conn)
    , table_(table)
{}

InsertBuilder& InsertBuilder::value(const std::string& column, const Value& val) {
    columns_.push_back(column);
    values_.push_back(val);
    return *this;
}

std::string InsertBuilder::toSql() const {
    std::ostringstream oss;
    oss << "INSERT INTO " << table_ << " (";

    for (size_t i = 0; i < columns_.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << columns_[i];
    }

    oss << ") VALUES (";

    for (size_t i = 0; i < columns_.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << "?";
    }

    oss << ")";
    return oss.str();
}

int64_t InsertBuilder::execute() {
    auto stmt = conn_.prepare(toSql());

    for (size_t i = 0; i < values_.size(); ++i) {
        stmt.bind(static_cast<int>(i + 1), values_[i]);
    }

    stmt.execute();
    return conn_.lastInsertRowId();
}

int64_t InsertBuilder::upsert() {
    std::ostringstream oss;
    oss << "INSERT OR REPLACE INTO " << table_ << " (";

    for (size_t i = 0; i < columns_.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << columns_[i];
    }

    oss << ") VALUES (";

    for (size_t i = 0; i < columns_.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << "?";
    }

    oss << ")";

    auto stmt = conn_.prepare(oss.str());

    for (size_t i = 0; i < values_.size(); ++i) {
        stmt.bind(static_cast<int>(i + 1), values_[i]);
    }

    stmt.execute();
    return conn_.lastInsertRowId();
}

// ========== BatchInsertBuilder ==========

BatchInsertBuilder::BatchInsertBuilder(Connection& conn, const std::string& table,
                                       const std::vector<std::string>& columns)
    : conn_(conn)
    , table_(table)
    , columns_(columns)
{}

BatchInsertBuilder& BatchInsertBuilder::addRow(const std::vector<Value>& values) {
    if (values.size() != columns_.size()) {
        throw QueryException(
            "Row value count (" + std::to_string(values.size()) +
            ") doesn't match column count (" + std::to_string(columns_.size()) + ")",
            ""
        );
    }
    rows_.push_back(values);
    return *this;
}

BatchInsertBuilder& BatchInsertBuilder::setBatchSize(size_t size) {
    batchSize_ = size;
    return *this;
}

BatchInsertBuilder& BatchInsertBuilder::clear() {
    rows_.clear();
    return *this;
}

int64_t BatchInsertBuilder::execute() {
    if (rows_.empty()) {
        return 0;
    }

    // Build the INSERT statement for a single row
    std::ostringstream oss;
    oss << "INSERT INTO " << table_ << " (";

    for (size_t i = 0; i < columns_.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << columns_[i];
    }

    oss << ") VALUES (";

    for (size_t i = 0; i < columns_.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << "?";
    }

    oss << ")";

    std::string sql = oss.str();

    int64_t totalInserted = 0;

    // Process in batches
    for (size_t batchStart = 0; batchStart < rows_.size(); batchStart += batchSize_) {
        size_t batchEnd = std::min(batchStart + batchSize_, rows_.size());

        // Each batch runs in a single transaction for performance
        Transaction txn(conn_);

        auto stmt = conn_.prepare(sql);

        for (size_t rowIdx = batchStart; rowIdx < batchEnd; ++rowIdx) {
            const auto& row = rows_[rowIdx];

            for (size_t colIdx = 0; colIdx < row.size(); ++colIdx) {
                stmt.bind(static_cast<int>(colIdx + 1), row[colIdx]);
            }

            stmt.execute();
            stmt.reset();
            stmt.clearBindings();
            ++totalInserted;
        }

        txn.commit();
    }

    return totalInserted;
}

} // namespace sqlite3db
