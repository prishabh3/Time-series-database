#ifndef COLUMNAR_STORAGE_H
#define COLUMNAR_STORAGE_H

#include "StockRecord.h"
#include "Compression.h"
#include "TSDBException.h"
#include <vector>
#include <string>
#include <memory>

namespace TSDB {

// Columnar storage: each attribute stored in its own contiguous array.
// Compression is transparent to callers — getRecord() / getAllRecords()
// automatically decompress on first access after compress() is called.
class ColumnarStorage {
private:
    std::vector<std::string> symbols;
    std::vector<int64_t>     timestamps;
    std::vector<double>      opens;
    std::vector<double>      highs;
    std::vector<double>      lows;
    // Mutable so that const accessors can trigger lazy decompression
    mutable std::vector<double>   closes;
    mutable std::vector<int64_t>  volumes;

    std::vector<uint8_t> compressedCloses;
    std::vector<uint8_t> compressedVolumes;

    mutable bool isCompressed;
    CompressionStats stats;

    // Restore closes/volumes from compressed buffers (called lazily)
    void decompressInPlace() const;

public:
    ColumnarStorage();

    void addRecord(const StockRecord& record);
    void addRecords(const std::vector<StockRecord>& records);

    // Returns a fully-populated record; triggers decompression if needed
    StockRecord getRecord(size_t index) const;

    std::vector<StockRecord> getAllRecords() const;

    // Column accessors — trigger decompression if needed
    const std::vector<double>&  getCloses()     const;
    const std::vector<int64_t>& getVolumes()    const;
    const std::vector<int64_t>& getTimestamps() const { return timestamps; }

    void compress();
    void decompress();

    bool compressed() const { return isCompressed; }

    CompressionStats getCompressionStats() const { return stats; }

    size_t size() const { return timestamps.size(); }
    size_t getMemoryUsage() const;

    void clear();
};

} // namespace TSDB

#endif // COLUMNAR_STORAGE_H
