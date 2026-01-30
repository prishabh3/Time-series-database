#ifndef BLOOMFILTER_H
#define BLOOMFILTER_H

#include <vector>
#include <string>
#include <functional>
#include <cstdint>

namespace TSDB {

// Bloom Filter for probabilistic existence checks
class BloomFilter {
private:
    std::vector<bool> bitArray;
    size_t numBits;
    size_t numHashFunctions;
    
    // Hash functions
    uint64_t hash1(const std::string& key) const;
    uint64_t hash2(const std::string& key) const;
    uint64_t nthHash(const std::string& key, size_t n) const;
    
public:
    // Constructor: size in bits, number of hash functions
    BloomFilter(size_t bits, size_t hashFuncs);
    
    // Add element
    void add(const std::string& key);
    
    // Check if element might exist (false positives possible, no false negatives)
    bool mightContain(const std::string& key) const;
    
    // Clear all bits
    void clear();
    
    // Get false positive rate estimate
    double estimateFalsePositiveRate() const;
    
    // Get number of elements added (approximate)
    size_t getSize() const;
    
    // Static helper: Calculate optimal parameters
    static size_t optimalNumBits(size_t expectedElements, double falsePositiveRate);
    static size_t optimalNumHashFunctions(size_t numBits, size_t expectedElements);
};

} // namespace TSDB

#endif // BLOOMFILTER_H
