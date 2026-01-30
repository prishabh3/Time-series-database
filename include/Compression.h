#ifndef COMPRESSION_H
#define COMPRESSION_H

#include <vector>
#include <cstdint>
#include <string>

namespace TSDB {

// Delta encoding: Store first value, then differences
class DeltaEncoder {
public:
    // Compress array of doubles (prices)
    static std::vector<uint8_t> compress(const std::vector<double>& values);
    
    // Decompress back to doubles
    static std::vector<double> decompress(const std::vector<uint8_t>& compressed);
    
    // Get compression ratio
    static double getCompressionRatio(size_t originalSize, size_t compressedSize);
};

// Run-length encoding: For repeating values
class RLEEncoder {
public:
    // Compress array of integers (volumes)
    static std::vector<uint8_t> compress(const std::vector<int64_t>& values);
    
    // Decompress back to integers
    static std::vector<int64_t> decompress(const std::vector<uint8_t>& compressed);
};

// Compression statistics
struct CompressionStats {
    size_t originalSize;
    size_t compressedSize;
    double compressionRatio;
    std::string algorithm;
    
    CompressionStats() : originalSize(0), compressedSize(0), compressionRatio(1.0) {}
};

} // namespace TSDB

#endif // COMPRESSION_H
