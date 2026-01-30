#include "BloomFilter.h"
#include <cmath>
#include <functional>

namespace TSDB {

BloomFilter::BloomFilter(size_t bits, size_t hashFuncs)
    : numBits(bits), numHashFunctions(hashFuncs) {
    bitArray.resize(numBits, false);
}

uint64_t BloomFilter::hash1(const std::string& key) const {
    std::hash<std::string> hasher;
    return hasher(key);
}

uint64_t BloomFilter::hash2(const std::string& key) const {
    uint64_t hash = 0;
    for (char c : key) {
        hash = hash * 31 + c;
    }
    return hash;
}

uint64_t BloomFilter::nthHash(const std::string& key, size_t n) const {
    // Combine two hash functions to generate n different hashes
    uint64_t h1 = hash1(key);
    uint64_t h2 = hash2(key);
    return (h1 + n * h2) % numBits;
}

void BloomFilter::add(const std::string& key) {
    for (size_t i = 0; i < numHashFunctions; i++) {
        uint64_t index = nthHash(key, i);
        bitArray[index] = true;
    }
}

bool BloomFilter::mightContain(const std::string& key) const {
    for (size_t i = 0; i < numHashFunctions; i++) {
        uint64_t index = nthHash(key, i);
        if (!bitArray[index]) {
            return false;  // Definitely not present
        }
    }
    return true;  // Might be present (false positive possible)
}

void BloomFilter::clear() {
    std::fill(bitArray.begin(), bitArray.end(), false);
}

double BloomFilter::estimateFalsePositiveRate() const {
    size_t setBits = 0;
    for (bool bit : bitArray) {
        if (bit) setBits++;
    }
    
    double loadFactor = static_cast<double>(setBits) / numBits;
    return std::pow(loadFactor, numHashFunctions);
}

size_t BloomFilter::getSize() const {
    size_t setBits = 0;
    for (bool bit : bitArray) {
        if (bit) setBits++;
    }
    return setBits;
}

size_t BloomFilter::optimalNumBits(size_t expectedElements, double falsePositiveRate) {
    // m = -(n * ln(p)) / (ln(2)^2)
    return static_cast<size_t>(
        -static_cast<double>(expectedElements) * std::log(falsePositiveRate) / 
        (std::log(2) * std::log(2))
    );
}

size_t BloomFilter::optimalNumHashFunctions(size_t numBits, size_t expectedElements) {
    // k = (m/n) * ln(2)
    return static_cast<size_t>(
        (static_cast<double>(numBits) / expectedElements) * std::log(2)
    );
}

} // namespace TSDB
