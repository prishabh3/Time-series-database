#ifndef TIMESERIESDB_H
#define TIMESERIESDB_H

#include "ColumnarStorage.h"
#include "BPlusTree.h"
#include "LRUCache.h"
#include "BloomFilter.h"
#include <unordered_map>
#include <shared_mutex>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <utility>

namespace TSDB {

// Query result
struct QueryResult {
    std::vector<StockRecord> records;
    double queryTimeMs;
    size_t recordsScanned;
    bool usedCache;

    QueryResult() : queryTimeMs(0), recordsScanned(0), usedCache(false) {}
};

// Main time-series database class
class TimeSeriesDB {
private:
    // Storage
    ColumnarStorage storage;

    // Timestamp index: timestamp -> record index (for cross-symbol range scans)
    BPlusTree<Timestamp, size_t> timestampIndex;

    // Per-symbol index: symbol -> sorted vector of (timestamp, record_index).
    // Kept sorted by timestamp so range queries can binary-search the boundaries.
    std::unordered_map<std::string, std::vector<std::pair<int64_t, size_t>>> symbolIndex;

    // Caching
    LRUCache<std::string, std::vector<StockRecord>> queryCache;

    // Single bloom filter over all known symbols for fast existence check
    BloomFilter symbolFilter;

    // Concurrency control
    mutable std::shared_mutex rwMutex;

    // Statistics (atomic: totalQueries is incremented outside the read lock)
    std::atomic<size_t> totalRecords;
    std::atomic<size_t> totalQueries;

    // Insert a record without acquiring the mutex (caller must hold write lock)
    void insertUnlocked(const StockRecord& record);

    std::string generateCacheKey(const std::string& query) const;
    void buildIndices();

public:
    explicit TimeSeriesDB(size_t cacheSize = 1000);

    void insert(const StockRecord& record);
    void bulkInsert(const std::vector<StockRecord>& records);
    bool loadFromCSV(const std::string& filename);

    QueryResult query(const std::string& symbol, int64_t startTime, int64_t endTime);
    QueryResult queryWithFilter(
        const std::string& symbol,
        int64_t startTime,
        int64_t endTime,
        std::function<bool(const StockRecord&)> filter);

    size_t countWhere(std::function<bool(const StockRecord&)> condition);
    std::vector<std::string> getSymbols() const;

    size_t getRecordCount() const { return totalRecords.load(); }
    size_t getQueryCount()  const { return totalQueries.load(); }
    double getCacheHitRate() const { return queryCache.getHitRate(); }

    CompressionStats getCompressionStats();
    void compress();
    void clear();
};

} // namespace TSDB

#endif // TIMESERIESDB_H
