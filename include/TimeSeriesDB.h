#ifndef TIMESERIESDB_H
#define TIMESERIESDB_H

#include "ColumnarStorage.h"
#include "BPlusTree.h"
#include "LRUCache.h"
#include "BloomFilter.h"
#include "Aggregation.h"
#include "TSDBException.h"
#include <unordered_map>
#include <shared_mutex>
#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace TSDB {

struct QueryResult {
    std::vector<StockRecord> records;
    double queryTimeMs    = 0;
    size_t recordsScanned = 0;
    bool   usedCache      = false;
};

class TimeSeriesDB {
private:
    ColumnarStorage storage;
    BPlusTree<Timestamp, size_t> timestampIndex;

    // Per-symbol: sorted vector of (timestamp, record_index) for O(log n) range scan
    std::unordered_map<std::string, std::vector<std::pair<int64_t, size_t>>> symbolIndex;

    LRUCache<std::string, std::vector<StockRecord>> queryCache;

    // Global bloom filter over symbol names — fast existence pre-check
    BloomFilter symbolFilter;

    mutable std::shared_mutex rwMutex;

    std::atomic<size_t> totalRecords{0};
    std::atomic<size_t> totalQueries{0};

    // Insert without acquiring the mutex (caller must hold write lock)
    void insertUnlocked(const StockRecord& record);

    std::string buildCacheKey(std::string_view symbol, int64_t start, int64_t end) const;
    void buildIndices();

public:
    explicit TimeSeriesDB(size_t cacheSize = 1000);

    // ── Write ────────────────────────────────────────────────────────────────
    void insert(const StockRecord& record);

    // Acquires a single write lock for the entire batch (much faster than N inserts)
    void bulkInsert(const std::vector<StockRecord>& records);

    // Returns false and throws CSVParseException on malformed rows
    bool loadFromCSV(const std::string& filename);

    // ── Point / range queries ─────────────────────────────────────────────────
    [[nodiscard]] QueryResult query(std::string_view symbol,
                                    int64_t startTime, int64_t endTime);

    [[nodiscard]] QueryResult queryWithFilter(
        std::string_view symbol,
        int64_t startTime, int64_t endTime,
        std::function<bool(const StockRecord&)> filter);

    // Query multiple symbols in one call; returns one QueryResult per symbol
    [[nodiscard]] std::unordered_map<std::string, QueryResult>
    queryMultiple(const std::vector<std::string>& symbols,
                  int64_t startTime, int64_t endTime);

    // ── Aggregation ───────────────────────────────────────────────────────────
    // Single-window OHLCV + VWAP over [startTime, endTime]
    [[nodiscard]] AggregateResult aggregate(std::string_view symbol,
                                             int64_t startTime, int64_t endTime);

    // Resample into fixed-size buckets (e.g. bucketMs = 86400000 for daily bars)
    [[nodiscard]] std::vector<AggregateResult>
    resample(std::string_view symbol,
             int64_t startTime, int64_t endTime, int64_t bucketMs);

    // ── Scan ─────────────────────────────────────────────────────────────────
    // Parallel full-table scan — splits work across hardware threads
    size_t countWhere(std::function<bool(const StockRecord&)> condition);

    // ── Metadata ─────────────────────────────────────────────────────────────
    std::vector<std::string> getSymbols() const;

    size_t getRecordCount() const { return totalRecords.load(); }
    size_t getQueryCount()  const { return totalQueries.load(); }
    double getCacheHitRate() const { return queryCache.getHitRate(); }

    // ── Storage management ────────────────────────────────────────────────────
    CompressionStats getCompressionStats();
    void compress();
    void decompress();
    void clear();
};

} // namespace TSDB

#endif // TIMESERIESDB_H
