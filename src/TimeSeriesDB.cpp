#include "TimeSeriesDB.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <ctime>

namespace TSDB {

// ─── Constructor ─────────────────────────────────────────────────────────────

TimeSeriesDB::TimeSeriesDB(size_t cacheSize)
    : queryCache(cacheSize),
      symbolFilter(100000, 7),   // ~100K symbols, 7 hash functions → <1% FP rate
      totalRecords(0),
      totalQueries(0) {}

// ─── Private helpers ─────────────────────────────────────────────────────────

void TimeSeriesDB::insertUnlocked(const StockRecord& record) {
    size_t index = storage.size();
    storage.addRecord(record);

    timestampIndex.insert(record.timestamp, index);

    // Keep the per-symbol list sorted by timestamp for binary-search range scans
    auto& entries = symbolIndex[record.symbol];
    auto  pos     = std::lower_bound(entries.begin(), entries.end(),
                                     std::make_pair(record.timestamp, size_t(0)));
    entries.insert(pos, {record.timestamp, index});

    // Global bloom filter: fast O(1) symbol-existence pre-check
    symbolFilter.add(record.symbol);

    ++totalRecords;
}

std::string TimeSeriesDB::generateCacheKey(const std::string& query) const {
    return query;
}

void TimeSeriesDB::buildIndices() {
    // Rebuilds both indexes from storage; used after bulk loads if needed.
    timestampIndex.clear();
    symbolIndex.clear();

    for (size_t i = 0; i < storage.size(); ++i) {
        StockRecord r = storage.getRecord(i);
        timestampIndex.insert(r.timestamp, i);
        auto& entries = symbolIndex[r.symbol];
        entries.push_back({r.timestamp, i});
    }

    for (auto& [sym, entries] : symbolIndex) {
        std::sort(entries.begin(), entries.end());
        symbolFilter.add(sym);
    }
}

// ─── Insert ──────────────────────────────────────────────────────────────────

void TimeSeriesDB::insert(const StockRecord& record) {
    std::unique_lock<std::shared_mutex> lock(rwMutex);
    insertUnlocked(record);
}

// Single write-lock for the whole batch — avoids N lock acquisitions
void TimeSeriesDB::bulkInsert(const std::vector<StockRecord>& records) {
    std::unique_lock<std::shared_mutex> lock(rwMutex);
    for (const auto& record : records) {
        insertUnlocked(record);
    }
}

// ─── CSV load ────────────────────────────────────────────────────────────────

bool TimeSeriesDB::loadFromCSV(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return false;

    std::string line;
    std::getline(file, line);  // Skip header

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string symbol, dateStr;
        double open, high, low, close;
        int64_t volume;

        if (std::getline(iss, symbol, ',') &&
            std::getline(iss, dateStr, ',') &&
            (iss >> open)  && iss.ignore() &&
            (iss >> high)  && iss.ignore() &&
            (iss >> low)   && iss.ignore() &&
            (iss >> close) && iss.ignore() &&
            (iss >> volume)) {

            // Parse "YYYY-MM-DD" into a UTC Unix timestamp (milliseconds)
            struct tm tm = {};
            std::istringstream ds(dateStr);
            ds >> std::get_time(&tm, "%Y-%m-%d");
            tm.tm_isdst = 0;
            time_t t = timegm(&tm);
            int64_t timestamp = static_cast<int64_t>(t) * 1000;

            StockRecord record(symbol, timestamp, open, high, low, close, volume);
            insert(record);
        }
    }

    return true;
}

// ─── Query ───────────────────────────────────────────────────────────────────

QueryResult TimeSeriesDB::query(const std::string& symbol, int64_t startTime, int64_t endTime) {
    ++totalQueries;

    auto start = std::chrono::high_resolution_clock::now();
    QueryResult result;

    // Cache key and lookup
    std::string cacheKey = symbol + "_" + std::to_string(startTime) + "_" + std::to_string(endTime);
    auto cached = queryCache.get(cacheKey);
    if (cached.has_value()) {
        result.records    = cached.value();
        result.usedCache  = true;
        result.recordsScanned = 0;
        auto end = std::chrono::high_resolution_clock::now();
        result.queryTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }

    std::shared_lock<std::shared_mutex> lock(rwMutex);

    // Fast bloom-filter pre-check: if the symbol was never inserted, skip immediately
    if (!symbolFilter.mightContain(symbol)) {
        auto end = std::chrono::high_resolution_clock::now();
        result.queryTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }

    auto it = symbolIndex.find(symbol);
    if (it == symbolIndex.end()) {
        auto end = std::chrono::high_resolution_clock::now();
        result.queryTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }

    // Binary-search the sorted (timestamp, index) list to find the range endpoints
    const auto& entries = it->second;
    auto lo = std::lower_bound(entries.begin(), entries.end(),
                               std::make_pair(startTime, size_t(0)));
    auto hi = std::upper_bound(entries.begin(), entries.end(),
                               std::make_pair(endTime, std::numeric_limits<size_t>::max()));

    result.recordsScanned = static_cast<size_t>(hi - lo);
    result.records.reserve(result.recordsScanned);

    for (auto eit = lo; eit != hi; ++eit) {
        result.records.push_back(storage.getRecord(eit->second));
    }

    lock.unlock();

    queryCache.put(cacheKey, result.records);

    auto end = std::chrono::high_resolution_clock::now();
    result.queryTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    return result;
}

// ─── Other public methods ────────────────────────────────────────────────────

QueryResult TimeSeriesDB::queryWithFilter(
    const std::string& symbol,
    int64_t startTime,
    int64_t endTime,
    std::function<bool(const StockRecord&)> filter) {

    auto result = query(symbol, startTime, endTime);

    std::vector<StockRecord> filtered;
    filtered.reserve(result.records.size());
    for (const auto& record : result.records) {
        if (filter(record)) filtered.push_back(record);
    }
    result.records = std::move(filtered);
    return result;
}

size_t TimeSeriesDB::countWhere(std::function<bool(const StockRecord&)> condition) {
    std::shared_lock<std::shared_mutex> lock(rwMutex);

    size_t count = 0;
    for (size_t i = 0; i < storage.size(); ++i) {
        if (condition(storage.getRecord(i))) ++count;
    }
    return count;
}

std::vector<std::string> TimeSeriesDB::getSymbols() const {
    std::shared_lock<std::shared_mutex> lock(rwMutex);

    std::vector<std::string> symbols;
    symbols.reserve(symbolIndex.size());
    for (const auto& [symbol, _] : symbolIndex) {
        symbols.push_back(symbol);
    }
    return symbols;
}

CompressionStats TimeSeriesDB::getCompressionStats() {
    std::shared_lock<std::shared_mutex> lock(rwMutex);
    return storage.getCompressionStats();
}

void TimeSeriesDB::compress() {
    std::unique_lock<std::shared_mutex> lock(rwMutex);
    storage.compress();
}

void TimeSeriesDB::clear() {
    std::unique_lock<std::shared_mutex> lock(rwMutex);
    storage.clear();
    timestampIndex.clear();
    symbolIndex.clear();
    symbolFilter.clear();
    queryCache.clear();
    totalRecords = 0;
    totalQueries = 0;
}

} // namespace TSDB
