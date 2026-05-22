#include "ColumnarStorage.h"
#include <algorithm>

namespace TSDB {

ColumnarStorage::ColumnarStorage() : isCompressed(false) {}

// ─── Write path ──────────────────────────────────────────────────────────────

void ColumnarStorage::addRecord(const StockRecord& record) {
    if (isCompressed) throw StorageCompressedException();

    symbols.push_back(record.symbol);
    timestamps.push_back(record.timestamp);
    opens.push_back(record.open);
    highs.push_back(record.high);
    lows.push_back(record.low);
    closes.push_back(record.close);
    volumes.push_back(record.volume);
}

void ColumnarStorage::addRecords(const std::vector<StockRecord>& records) {
    for (const auto& record : records) addRecord(record);
}

// ─── Lazy decompression (const, uses mutable members) ────────────────────────

void ColumnarStorage::decompressInPlace() const {
    closes  = DeltaEncoder::decompress(compressedCloses);
    volumes = RLEEncoder::decompress(compressedVolumes);

    // Release compressed buffers once data is restored
    const_cast<std::vector<uint8_t>&>(compressedCloses).clear();
    const_cast<std::vector<uint8_t>&>(compressedCloses).shrink_to_fit();
    const_cast<std::vector<uint8_t>&>(compressedVolumes).clear();
    const_cast<std::vector<uint8_t>&>(compressedVolumes).shrink_to_fit();

    isCompressed = false;
}

// ─── Read path ───────────────────────────────────────────────────────────────

StockRecord ColumnarStorage::getRecord(size_t index) const {
    if (index >= timestamps.size()) return StockRecord();
    if (isCompressed) decompressInPlace();

    return StockRecord(
        symbols[index], timestamps[index],
        opens[index], highs[index], lows[index],
        closes[index], volumes[index]);
}

std::vector<StockRecord> ColumnarStorage::getAllRecords() const {
    if (isCompressed) decompressInPlace();

    std::vector<StockRecord> records;
    records.reserve(timestamps.size());
    for (size_t i = 0; i < timestamps.size(); ++i) {
        records.emplace_back(
            symbols[i], timestamps[i],
            opens[i], highs[i], lows[i],
            closes[i], volumes[i]);
    }
    return records;
}

const std::vector<double>& ColumnarStorage::getCloses() const {
    if (isCompressed) decompressInPlace();
    return closes;
}

const std::vector<int64_t>& ColumnarStorage::getVolumes() const {
    if (isCompressed) decompressInPlace();
    return volumes;
}

// ─── Compression ─────────────────────────────────────────────────────────────

void ColumnarStorage::compress() {
    if (isCompressed) return;

    size_t originalSize = closes.size() * sizeof(double) +
                          volumes.size() * sizeof(int64_t);

    compressedCloses  = DeltaEncoder::compress(closes);
    compressedVolumes = RLEEncoder::compress(volumes);

    size_t compressedSize = compressedCloses.size() + compressedVolumes.size();
    stats.originalSize    = originalSize;
    stats.compressedSize  = compressedSize;
    stats.compressionRatio = DeltaEncoder::getCompressionRatio(originalSize, compressedSize);
    stats.algorithm       = "Delta + RLE";

    closes.clear();
    closes.shrink_to_fit();
    volumes.clear();
    volumes.shrink_to_fit();

    isCompressed = true;
}

void ColumnarStorage::decompress() {
    if (!isCompressed) return;
    decompressInPlace();
}

// ─── Utilities ───────────────────────────────────────────────────────────────

size_t ColumnarStorage::getMemoryUsage() const {
    size_t usage = 0;
    usage += symbols.capacity()     * sizeof(std::string);
    usage += timestamps.capacity()  * sizeof(int64_t);
    usage += opens.capacity()       * sizeof(double);
    usage += highs.capacity()       * sizeof(double);
    usage += lows.capacity()        * sizeof(double);
    usage += closes.capacity()      * sizeof(double);
    usage += volumes.capacity()     * sizeof(int64_t);
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
