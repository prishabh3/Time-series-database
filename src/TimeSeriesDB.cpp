#include "TimeSeriesDB.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>

namespace TSDB {

TimeSeriesDB::TimeSeriesDB(size_t cacheSize)
    : queryCache(cacheSize), totalRecords(0), totalQueries(0) {}

void TimeSeriesDB::insert(const StockRecord& record) {
    std::unique_lock<std::shared_mutex> lock(rwMutex);
    
    size_t index = storage.size();
    storage.addRecord(record);
    
    // Update timestamp index
    timestampIndex.insert(record.timestamp, index);
    
    // Update symbol index
    symbolIndex[record.symbol].push_back(index);
    
    // Update bloom filter
    if (symbolFilters.find(record.symbol) == symbolFilters.end()) {
        symbolFilters[record.symbol] = std::make_shared<BloomFilter>(10000, 3);
    }
    symbolFilters[record.symbol]->add(std::to_string(record.timestamp));
    
    totalRecords++;
}

void TimeSeriesDB::bulkInsert(const std::vector<StockRecord>& records) {
    for (const auto& record : records) {
        insert(record);
    }
}

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
        
        // Parse CSV: symbol,date,open,high,low,close,volume
        if (std::getline(iss, symbol, ',') &&
            std::getline(iss, dateStr, ',') &&
            (iss >> open) && iss.ignore() &&
            (iss >> high) && iss.ignore() &&
            (iss >> low) && iss.ignore() &&
            (iss >> close) && iss.ignore() &&
            (iss >> volume)) {
            
            // Convert date to timestamp (simplified)
            int64_t timestamp = std::hash<std::string>{}(dateStr);  // Placeholder
            
            StockRecord record(symbol, timestamp, open, high, low, close, volume);
            insert(record);
        }
    }
    
    file.close();
    return true;
}

std::string TimeSeriesDB::generateCacheKey(const std::string& query) const {
    return query;  // Simple implementation
}

QueryResult TimeSeriesDB::query(const std::string& symbol, int64_t startTime, int64_t endTime) {
    totalQueries++;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    QueryResult result;
    
    // Generate cache key
    std::string cacheKey = symbol + "_" + std::to_string(startTime) + "_" + std::to_string(endTime);
    
    // Check cache
    auto cached = queryCache.get(cacheKey);
    if (cached.has_value()) {
        result.records = cached.value();
        result.usedCache = true;
        result.recordsScanned = 0;
        
        auto end = std::chrono::high_resolution_clock::now();
        result.queryTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
    
    // Query from storage
    std::shared_lock<std::shared_mutex> lock(rwMutex);
    
    // Check bloom filter
    if (symbolFilters.find(symbol) == symbolFilters.end()) {
        result.queryTimeMs = 0;
        return result;  // Symbol doesn't exist
    }
    
    // Get indices for this symbol
    auto it = symbolIndex.find(symbol);
    if (it == symbolIndex.end()) {
        result.queryTimeMs = 0;
        return result;
    }
    
    // Filter by time range
    for (size_t idx : it->second) {
        StockRecord record = storage.getRecord(idx);
        result.recordsScanned++;
        
        if (record.timestamp >= startTime && record.timestamp <= endTime) {
            result.records.push_back(record);
        }
    }
    
    lock.unlock();
    
    // Cache result
    queryCache.put(cacheKey, result.records);
    
    auto end = std::chrono::high_resolution_clock::now();
    result.queryTimeMs = std::chrono::duration<double, std::milli>(end - start).count();
    
    return result;
}

QueryResult TimeSeriesDB::queryWithFilter(
    const std::string& symbol,
    int64_t startTime,
    int64_t endTime,
    std::function<bool(const StockRecord&)> filter) {
    
    auto result = query(symbol, startTime, endTime);
    
    // Apply additional filter
    std::vector<StockRecord> filtered;
    for (const auto& record : result.records) {
        if (filter(record)) {
            filtered.push_back(record);
        }
    }
    
    result.records = filtered;
    return result;
}

size_t TimeSeriesDB::countWhere(std::function<bool(const StockRecord&)> condition) {
    std::shared_lock<std::shared_mutex> lock(rwMutex);
    
    size_t count = 0;
    for (size_t i = 0; i < storage.size(); i++) {
        auto record = storage.getRecord(i);
        if (condition(record)) {
            count++;
        }
    }
    
    return count;
}

std::vector<std::string> TimeSeriesDB::getSymbols() const {
    std::shared_lock<std::shared_mutex> lock(rwMutex);
    
    std::vector<std::string> symbols;
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
    symbolFilters.clear();
    queryCache.clear();
    totalRecords = 0;
    totalQueries = 0;
}

} // namespace TSDB
