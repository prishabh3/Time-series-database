#include "Compression.h"
#include <cstring>
#include <cmath>
#include <climits>

namespace TSDB {

// Delta encoding for doubles (prices)
std::vector<uint8_t> DeltaEncoder::compress(const std::vector<double>& values) {
    if (values.empty()) return {};
    
    std::vector<uint8_t> compressed;
    
    // Store first value as-is (8 bytes)
    double firstValue = values[0];
    const uint8_t* firstBytes = reinterpret_cast<const uint8_t*>(&firstValue);
    compressed.insert(compressed.end(), firstBytes, firstBytes + sizeof(double));
    
    // Store deltas as 32-bit integers (scaled by 10000 for 4 decimal places of precision).
    // Clamp to int32_t range to avoid undefined behaviour on overflow; values outside
    // ±214748 will lose sub-cent precision but decompress without UB.
    for (size_t i = 1; i < values.size(); i++) {
        double delta = values[i] - values[i-1];
        double scaled = delta * 10000.0;
        if (scaled > static_cast<double>(INT32_MAX)) scaled = static_cast<double>(INT32_MAX);
        if (scaled < static_cast<double>(INT32_MIN)) scaled = static_cast<double>(INT32_MIN);
        int32_t scaledDelta = static_cast<int32_t>(scaled);

        const uint8_t* deltaBytes = reinterpret_cast<const uint8_t*>(&scaledDelta);
        compressed.insert(compressed.end(), deltaBytes, deltaBytes + sizeof(int32_t));
    }
    
    return compressed;
}

std::vector<double> DeltaEncoder::decompress(const std::vector<uint8_t>& compressed) {
    if (compressed.size() < sizeof(double)) return {};
    
    std::vector<double> values;
    
    // Read first value
    double currentValue;
    std::memcpy(&currentValue, compressed.data(), sizeof(double));
    values.push_back(currentValue);
    
    // Read deltas and reconstruct values
    size_t offset = sizeof(double);
    while (offset + sizeof(int32_t) <= compressed.size()) {
        int32_t scaledDelta;
        std::memcpy(&scaledDelta, compressed.data() + offset, sizeof(int32_t));
        
        double delta = scaledDelta / 10000.0;
        currentValue += delta;
        values.push_back(currentValue);
        
        offset += sizeof(int32_t);
    }
    
    return values;
}

double DeltaEncoder::getCompressionRatio(size_t originalSize, size_t compressedSize) {
    if (compressedSize == 0) return 0.0;
    return static_cast<double>(originalSize) / compressedSize;
}

// Run-length encoding for integers (volumes)
std::vector<uint8_t> RLEEncoder::compress(const std::vector<int64_t>& values) {
    if (values.empty()) return {};
    
    std::vector<uint8_t> compressed;
    
    int64_t currentValue = values[0];
    int32_t count = 1;
    
    for (size_t i = 1; i < values.size(); i++) {
        if (values[i] == currentValue && count < INT32_MAX) {
            count++;
        } else {
            // Write value and count
            const uint8_t* valueBytes = reinterpret_cast<const uint8_t*>(&currentValue);
            compressed.insert(compressed.end(), valueBytes, valueBytes + sizeof(int64_t));
            
            const uint8_t* countBytes = reinterpret_cast<const uint8_t*>(&count);
            compressed.insert(compressed.end(), countBytes, countBytes + sizeof(int32_t));
            
            currentValue = values[i];
            count = 1;
        }
    }
    
    // Write last value and count
    const uint8_t* valueBytes = reinterpret_cast<const uint8_t*>(&currentValue);
    compressed.insert(compressed.end(), valueBytes, valueBytes + sizeof(int64_t));
    
    const uint8_t* countBytes = reinterpret_cast<const uint8_t*>(&count);
    compressed.insert(compressed.end(), countBytes, countBytes + sizeof(int32_t));
    
    return compressed;
}

std::vector<int64_t> RLEEncoder::decompress(const std::vector<uint8_t>& compressed) {
    std::vector<int64_t> values;
    
    size_t offset = 0;
    while (offset + sizeof(int64_t) + sizeof(int32_t) <= compressed.size()) {
        int64_t value;
        std::memcpy(&value, compressed.data() + offset, sizeof(int64_t));
        offset += sizeof(int64_t);
        
        int32_t count;
        std::memcpy(&count, compressed.data() + offset, sizeof(int32_t));
        offset += sizeof(int32_t);
        
        for (int32_t i = 0; i < count; i++) {
            values.push_back(value);
        }
    }
    
    return values;
}

} // namespace TSDB
