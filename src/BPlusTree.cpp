#include "BPlusTree.h"

namespace TSDB {

// Template implementations must be in header or explicitly instantiated
// For now, providing explicit instantiation for common types

template<typename KeyType, typename ValueType>
BPlusTree<KeyType, ValueType>::BPlusTree() : count(0) {
    root = std::make_shared<Node>(true);  // Start with empty leaf
}

template<typename KeyType, typename ValueType>
void BPlusTree<KeyType, ValueType>::insert(const KeyType& key, const ValueType& value) {
    // Simplified insertion for demonstration
    // In production, this would handle node splitting properly
    if (root->keys.empty()) {
        root->keys.push_back(key);
        root->values.push_back(value);
        count++;
        return;
    }
    
    insertNonFull(root, key, value);
    count++;
}

template<typename KeyType, typename ValueType>
void BPlusTree<KeyType, ValueType>::insertNonFull(std::shared_ptr<Node> node, const KeyType& key, const ValueType& value) {
    if (node->isLeaf) {
        // Insert in sorted order
        auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
        size_t pos = it - node->keys.begin();
        node->keys.insert(it, key);
        node->values.insert(node->values.begin() + pos, value);
    }
}

template<typename KeyType, typename ValueType>
bool BPlusTree<KeyType, ValueType>::find(const KeyType& key, ValueType& value) const {
    if (!root || root->keys.empty()) return false;
    
    // Simple linear search in leaf (for demonstration)
    for (size_t i = 0; i < root->keys.size(); i++) {
        if (root->keys[i] == key) {
            value = root->values[i];
            return true;
        }
    }
    return false;
}

template<typename KeyType, typename ValueType>
std::vector<ValueType> BPlusTree<KeyType, ValueType>::rangeQuery(const KeyType& minKey, const KeyType& maxKey) const {
    std::vector<ValueType> result;
    
    if (!root) return result;
    
    for (size_t i = 0; i < root->keys.size(); i++) {
        if (root->keys[i] >= minKey && root->keys[i] <= maxKey) {
            result.push_back(root->values[i]);
        }
    }
    
    return result;
}

template<typename KeyType, typename ValueType>
std::vector<ValueType> BPlusTree<KeyType, ValueType>::getAllValues() const {
    if (!root) return {};
    return root->values;
}

template<typename KeyType, typename ValueType>
void BPlusTree<KeyType, ValueType>::clear() {
    root = std::make_shared<Node>(true);
    count = 0;
}

template<typename KeyType, typename ValueType>
void BPlusTree<KeyType, ValueType>::splitChild(std::shared_ptr<Node> parent, int index) {
    // Simplified - full implementation would handle complex node splitting
    (void)parent;  // Mark as intentionally unused
    (void)index;   // Mark as intentionally unused
}

// Explicit instantiation for common types
template class BPlusTree<int64_t, int>;
template class BPlusTree<int64_t, size_t>;
// Note: Timestamp is typedef'd to int64_t, so no separate instantiation needed

} // namespace TSDB

