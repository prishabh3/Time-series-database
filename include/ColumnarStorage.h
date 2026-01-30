#ifndef COLUMNAR_STORAGE_H
#define COLUMNAR_STORAGE_H

#include "StockRecord.h"
#include "Compression.h"
#include <vector>
#include <string>
#include <memory>

namespace TSDB {

// Columnar storage: Each attribute stored in separate array
class ColumnarStorage {
private:
    std::vector<std::string> symbols;
    std::vector<int64_t> timestamps;
    std::vector<double> opens;
    std::vector<double> highs;
    std::vector<double> lows;
    std::vector<double> closes;
    std::vector<int64_t> volumes;
    
    // Compressed versions
    std::vector<uint8_t> compressedCloses;
    std::vector<uint8_t> compressedVolumes;
    
    bool isCompressed;
    CompressionStats stats;
    
public:
    ColumnarStorage();
    
    // Add record
    void addRecord(const StockRecord& record);
    
    // Add multiple records
    void addRecords(const std::vector<StockRecord>& records);
    
    // Get record by index
    StockRecord getRecord(size_t index) const;
    
    // Get all records
    std::vector<StockRecord> getAllRecords() const;
    
    // Get specific column (for efficient analytical queries)
    const std::vector<double>& getCloses() const { return closes; }
    const std::vector<int64_t>& getVolumes() const { return volumes; }
    const std::vector<int64_t>& getTimestamps() const { return timestamps; }
    
    // Compress data
    void compress();
    
    // Decompress data
    void decompress();
    
    // Get compression stats
    CompressionStats getCompressionStats() const { return stats; }
    
    // Get size
    size_t size() const { return timestamps.size(); }
    
    // Get memory usage
    size_t getMemoryUsage() const;
    
    // Clear all data
    void clear();
};

} // namespace TSDB

#endif // COLUMNAR_STORAGE_H
