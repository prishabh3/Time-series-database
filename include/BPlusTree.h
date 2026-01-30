#ifndef BPLUSTREE_H
#define BPLUSTREE_H

#include <vector>
#include <memory>
#include <functional>

namespace TSDB {

// B+ Tree for efficient range queries on timestamps
template<typename KeyType, typename ValueType>
class BPlusTree {
private:
    static const int ORDER = 100;  // Maximum children per node
    
    struct Node {
        bool isLeaf;
        std::vector<KeyType> keys;
        std::vector<ValueType> values;  // Only in leaf nodes
        std::vector<std::shared_ptr<Node>> children;  // Only in internal nodes
        std::shared_ptr<Node> next;  // Link to next leaf for range scans
        
        Node(bool leaf = false) : isLeaf(leaf) {}
    };
    
    std::shared_ptr<Node> root;
    size_t count;
    
    void splitChild(std::shared_ptr<Node> parent, int index);
    void insertNonFull(std::shared_ptr<Node> node, const KeyType& key, const ValueType& value);
    
public:
    BPlusTree();
    
    // Insert key-value pair
    void insert(const KeyType& key, const ValueType& value);
    
    // Find value by exact key
    bool find(const KeyType& key, ValueType& value) const;
    
    // Range query: find all values where minKey <= key <= maxKey
    std::vector<ValueType> rangeQuery(const KeyType& minKey, const KeyType& maxKey) const;
    
    // Get all values in order
    std::vector<ValueType> getAllValues() const;
    
    // Size of tree
    size_t size() const { return count; }
    
    // Check if empty
    bool empty() const { return count == 0; }
    
    // Clear tree
    void clear();
};

// Timestamp type for indexing
using Timestamp = int64_t;  // Unix timestamp in milliseconds

} // namespace TSDB

#endif // BPLUSTREE_H
