#ifndef LRUCACHE_H
#define LRUCACHE_H

#include <unordered_map>
#include <list>
#include <mutex>
#include <optional>

namespace TSDB {

// LRU Cache: O(1) get/put using hash map + doubly-linked list
template<typename KeyType, typename ValueType>
class LRUCache {
private:
    size_t capacity;
    std::list<std::pair<KeyType, ValueType>> itemList;  // Doubly-linked list
    std::unordered_map<KeyType, typename std::list<std::pair<KeyType, ValueType>>::iterator> itemMap;
    
    mutable std::mutex cacheMutex;
    
    // Statistics
    size_t hits;
    size_t misses;
    
public:
    LRUCache(size_t cap);
    
    // Get value by key, returns nullopt if not found
    std::optional<ValueType> get(const KeyType& key);
    
    // Put key-value pair, evicts LRU if full
    void put(const KeyType& key, const ValueType& value);
    
    // Check if key exists
    bool contains(const KeyType& key) const;
    
    // Remove key
    void remove(const KeyType& key);
    
    // Clear cache
    void clear();
    
    // Get current size
    size_t size() const;
    
    // Get capacity
    size_t getCapacity() const { return capacity; }
    
    // Get cache hit rate
    double getHitRate() const;
    
    // Get statistics
    void getStats(size_t& totalHits, size_t& totalMisses) const;
    
    // Reset statistics
    void resetStats();
};

} // namespace TSDB

#endif // LRUCACHE_H
