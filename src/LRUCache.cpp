#include "LRUCache.h"
#include "StockRecord.h"  // Needed for template instantiation

namespace TSDB {

template<typename KeyType, typename ValueType>
LRUCache<KeyType, ValueType>::LRUCache(size_t cap) 
    : capacity(cap), hits(0), misses(0) {}

template<typename KeyType, typename ValueType>
std::optional<ValueType> LRUCache<KeyType, ValueType>::get(const KeyType& key) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    auto it = itemMap.find(key);
    if (it == itemMap.end()) {
        misses++;
        return std::nullopt;  // Cache miss
    }
    
    // Move to front (most recently used)
    itemList.splice(itemList.begin(), itemList, it->second);
    hits++;
    
    return it->second->second;
}

template<typename KeyType, typename ValueType>
void LRUCache<KeyType, ValueType>::put(const KeyType& key, const ValueType& value) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    // Check if key already exists
    auto it = itemMap.find(key);
    if (it != itemMap.end()) {
        // Update value and move to front
        it->second->second = value;
        itemList.splice(itemList.begin(), itemList, it->second);
        return;
    }
    
    // Check if cache is full
    if (itemList.size() >= capacity) {
        // Remove least recently used (back of list)
        auto last = itemList.back();
        itemMap.erase(last.first);
        itemList.pop_back();
    }
    
    // Add new item to front
    itemList.emplace_front(key, value);
    itemMap[key] = itemList.begin();
}

template<typename KeyType, typename ValueType>
bool LRUCache<KeyType, ValueType>::contains(const KeyType& key) const {
    std::lock_guard<std::mutex> lock(cacheMutex);
    return itemMap.find(key) != itemMap.end();
}

template<typename KeyType, typename ValueType>
void LRUCache<KeyType, ValueType>::remove(const KeyType& key) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    auto it = itemMap.find(key);
    if (it != itemMap.end()) {
        itemList.erase(it->second);
        itemMap.erase(it);
    }
}

template<typename KeyType, typename ValueType>
void LRUCache<KeyType, ValueType>::clear() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    itemList.clear();
    itemMap.clear();
}

template<typename KeyType, typename ValueType>
size_t LRUCache<KeyType, ValueType>::size() const {
    std::lock_guard<std::mutex> lock(cacheMutex);
    return itemList.size();
}

template<typename KeyType, typename ValueType>
double LRUCache<KeyType, ValueType>::getHitRate() const {
    std::lock_guard<std::mutex> lock(cacheMutex);
    size_t total = hits + misses;
    if (total == 0) return 0.0;
    return static_cast<double>(hits) / total;
}

template<typename KeyType, typename ValueType>
void LRUCache<KeyType, ValueType>::getStats(size_t& totalHits, size_t& totalMisses) const {
    std::lock_guard<std::mutex> lock(cacheMutex);
    totalHits = hits;
    totalMisses = misses;
}

template<typename KeyType, typename ValueType>
void LRUCache<KeyType, ValueType>::resetStats() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    hits = 0;
    misses = 0;
}

// Explicit instantiation for common types
template class LRUCache<std::string, std::vector<StockRecord>>;
template class LRUCache<int64_t, StockRecord>;
template class LRUCache<int, int>;

} // namespace TSDB
