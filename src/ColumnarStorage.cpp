#include "ColumnarStorage.h"
#include <algorithm>

namespace TSDB {

ColumnarStorage::ColumnarStorage() : isCompressed(false) {}

void ColumnarStorage::addRecord(const StockRecord& record) {
    symbols.push_back(record.symbol);
    timestamps.push_back(record.timestamp);
    opens.push_back(record.open);
    highs.push_back(record.high);
    lows.push_back(record.low);
    closes.push_back(record.close);
    volumes.push_back(record.volume);
}

void ColumnarStorage::addRecords(const std::vector<StockRecord>& records) {
    for (const auto& record : records) {
        addRecord(record);
    }
}

StockRecord ColumnarStorage::getRecord(size_t index) const {
    if (index >= timestamps.size()) {
        return StockRecord();
    }

    // closes and volumes are cleared during compression; restore them first
    if (isCompressed) {
        const_cast<ColumnarStorage*>(this)->decompress();
    }

    return StockRecord(
        symbols[index],
        timestamps[index],
        opens[index],
        highs[index],
        lows[index],
        closes[index],
        volumes[index]
    );
}

std::vector<StockRecord> ColumnarStorage::getAllRecords() const {
    std::vector<StockRecord> records;
    records.reserve(timestamps.size());
    
    for (size_t i = 0; i < timestamps.size(); i++) {
        records.push_back(getRecord(i));
    }
    
    return records;
}

void ColumnarStorage::compress() {
    if (isCompressed) return;
    
    size_t originalSize = closes.size() * sizeof(double) + volumes.size() * sizeof(int64_t);
    
    // Compress prices using delta encoding
    compressedCloses = DeltaEncoder::compress(closes);
    
    // Compress volumes using RLE
    compressedVolumes = RLEEncoder::compress(volumes);
    
    size_t compressedSize = compressedCloses.size() + compressedVolumes.size();
    
    stats.originalSize = originalSize;
    stats.compressedSize = compressedSize;
    stats.compressionRatio = DeltaEncoder::getCompressionRatio(originalSize, compressedSize);
    stats.algorithm = "Delta + RLE";
    
    isCompressed = true;
    
    // Clear uncompressed data to save memory
    closes.clear();
    volumes.clear();
}

void ColumnarStorage::decompress() {
    if (!isCompressed) return;

    closes  = DeltaEncoder::decompress(compressedCloses);
    volumes = RLEEncoder::decompress(compressedVolumes);

    // Release compressed buffers — data now lives in closes/volumes
    compressedCloses.clear();
    compressedCloses.shrink_to_fit();
    compressedVolumes.clear();
    compressedVolumes.shrink_to_fit();

    isCompressed = false;
}

size_t ColumnarStorage::getMemoryUsage() const {
    size_t usage = 0;
    
    // Calculate size of each vector
    usage += symbols.capacity() * sizeof(std::string);  // Approximate
    usage += timestamps.capacity() * sizeof(int64_t);
    usage += opens.capacity() * sizeof(double);
    usage += highs.capacity() * sizeof(double);
    usage += lows.capacity() * sizeof(double);
    usage += closes.capacity() * sizeof(double);
    usage += volumes.capacity() * sizeof(int64_t);
    
    // Compressed data
    usage += compressedCloses.capacity();
    usage += compressedVolumes.capacity();
    
    return usage;
}

void ColumnarStorage::clear() {
    symbols.clear();
    timestamps.clear();
    opens.clear();
    highs.clear();
    lows.clear();
    closes.clear();
    volumes.clear();
    compressedCloses.clear();
    compressedVolumes.clear();
    isCompressed = false;
}

} // namespace TSDB
