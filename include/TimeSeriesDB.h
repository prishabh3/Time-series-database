#ifndef TIMESERIESDB_H
#define TIMESERIESDB_H

#include "ColumnarStorage.h"
#include "BPlusTree.h"
#include "LRUCache.h"
#include "BloomFilter.h"
#include <unordered_map>
#include <shared_mutex>
#include <string>
#include <vector>

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
    
    // Indexing
    BPlusTree<Timestamp, size_t> timestampIndex;  // timestamp -> record index
    std::unordered_map<std::string, std::vector<size_t>> symbolIndex;  // symbol -> record indices
    
    // Caching
    LRUCache<std::string, std::vector<StockRecord>> queryCache;
    
    // Bloom filters (one per symbol for quick existence checks)
    std::unordered_map<std::string, std::shared_ptr<BloomFilter>> symbolFilters;
    
    // Concurrency control
    mutable std::shared_mutex rwMutex;
    
    // Statistics
    size_t totalRecords;
    size_t totalQueries;
    
    // Helper methods
    std::string generateCacheKey(const std::string& query) const;
    void buildIndices();
    
public:
    TimeSeriesDB(size_t cacheSize = 1000);
    
    // Insert single record
    void insert(const StockRecord& record);
    
    // Bulk insert
    void bulkInsert(const std::vector<StockRecord>& records);
    
    // Load from CSV file
    bool loadFromCSV(const std::string& filename);
    
    // Query by symbol and date range
    QueryResult query(const std::string& symbol, int64_t startTime, int64_t endTime);
    
    // Complex query: filter by conditions
    QueryResult queryWithFilter(
        const std::string& symbol,
        int64_t startTime,
        int64_t endTime,
        std::function<bool(const StockRecord&)> filter
    );
    
    // Aggregate query: count records matching condition
    size_t countWhere(std::function<bool(const StockRecord&)> condition);
    
    // Get all unique symbols
    std::vector<std::string> getSymbols() const;
    
    // Get statistics
    size_t getRecordCount() const { return totalRecords; }
    size_t getQueryCount() const { return totalQueries; }
    double getCacheHitRate() const { return queryCache.getHitRate(); }
    
    // Get compression stats
    CompressionStats getCompressionStats();
    
    // Compress data
    void compress();
    
    // Clear all data
    void clear();
};

} // namespace TSDB

#endif // TIMESERIESDB_H
