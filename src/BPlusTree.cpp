#include "BPlusTree.h"
#include <algorithm>
#include <stdexcept>

namespace TSDB {

template<typename KeyType, typename ValueType>
BPlusTree<KeyType, ValueType>::BPlusTree() : count(0) {
    root = std::make_shared<Node>(true);
}

// ─── Internal helpers ────────────────────────────────────────────────────────

template<typename KeyType, typename ValueType>
void BPlusTree<KeyType, ValueType>::splitChild(std::shared_ptr<Node> parent, int index) {
    auto child   = parent->children[index];
    auto sibling = std::make_shared<Node>(child->isLeaf);

    // For ORDER=100: a full node has ORDER-1 = 99 keys.
    // mid splits it into [0, mid) and [mid, 99).
    int mid = (ORDER - 1) / 2;  // == 49

    if (child->isLeaf) {
        // Leaf split: copy upper half to sibling, keep lower half in child.
        // The first key of sibling is promoted to parent (copy-up, not push-up).
        sibling->keys.assign(child->keys.begin() + mid, child->keys.end());
        sibling->values.assign(child->values.begin() + mid, child->values.end());
        child->keys.resize(mid);
        child->values.resize(mid);

        // Maintain leaf chain
        sibling->next = child->next;
        child->next   = sibling;

        // Promote the first key of sibling up to parent
        parent->keys.insert(parent->keys.begin() + index, sibling->keys[0]);
    } else {
        // Internal split: middle key is pushed up (not copied).
        KeyType midKey = child->keys[mid];

        sibling->keys.assign(child->keys.begin() + mid + 1, child->keys.end());
        sibling->children.assign(child->children.begin() + mid + 1, child->children.end());
        child->keys.resize(mid);
        child->children.resize(mid + 1);

        parent->keys.insert(parent->keys.begin() + index, midKey);
    }

    parent->children.insert(parent->children.begin() + index + 1, sibling);
}

template<typename KeyType, typename ValueType>
void BPlusTree<KeyType, ValueType>::insertNonFull(std::shared_ptr<Node> node,
                                                   const KeyType& key,
                                                   const ValueType& value) {
    if (node->isLeaf) {
        auto it  = std::lower_bound(node->keys.begin(), node->keys.end(), key);
        size_t pos = static_cast<size_t>(it - node->keys.begin());
        node->keys.insert(it, key);
        node->values.insert(node->values.begin() + pos, value);
        return;
    }

    // Find which child to descend into
    auto it       = std::upper_bound(node->keys.begin(), node->keys.end(), key);
    size_t childIdx = static_cast<size_t>(it - node->keys.begin());

    auto child = node->children[childIdx];
    if (static_cast<int>(child->keys.size()) == ORDER - 1) {
        splitChild(node, static_cast<int>(childIdx));
        // After split the promoted key sits at node->keys[childIdx];
        // decide which of the two resulting children to descend into.
        if (key >= node->keys[childIdx]) {
            ++childIdx;
        }
    }
    insertNonFull(node->children[childIdx], key, value);
}

// ─── Public API ──────────────────────────────────────────────────────────────

template<typename KeyType, typename ValueType>
void BPlusTree<KeyType, ValueType>::insert(const KeyType& key, const ValueType& value) {
    if (static_cast<int>(root->keys.size()) == ORDER - 1) {
        // Root is full — grow the tree upward
        auto newRoot = std::make_shared<Node>(false);
        newRoot->children.push_back(root);
        splitChild(newRoot, 0);
        root = newRoot;
    }
    insertNonFull(root, key, value);
    ++count;
}

template<typename KeyType, typename ValueType>
bool BPlusTree<KeyType, ValueType>::find(const KeyType& key, ValueType& value) const {
    if (!root || root->keys.empty()) return false;

    auto node = root;
    while (!node->isLeaf) {
        auto it      = std::upper_bound(node->keys.begin(), node->keys.end(), key);
        size_t idx   = static_cast<size_t>(it - node->keys.begin());
        node         = node->children[idx];
    }

    auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
    if (it != node->keys.end() && *it == key) {
        value = node->values[static_cast<size_t>(it - node->keys.begin())];
        return true;
    }
    return false;
}

template<typename KeyType, typename ValueType>
std::vector<ValueType> BPlusTree<KeyType, ValueType>::rangeQuery(
    const KeyType& minKey, const KeyType& maxKey) const {

    std::vector<ValueType> result;
    if (!root) return result;

    // Descend to the leftmost leaf that could hold minKey
    auto node = root;
    while (!node->isLeaf) {
        auto it    = std::lower_bound(node->keys.begin(), node->keys.end(), minKey);
        size_t idx = static_cast<size_t>(it - node->keys.begin());
        // lower_bound gives first key >= minKey; we go left of it
        if (idx > 0 && node->keys[idx - 1] == minKey) --idx;
        node = node->children[idx];
    }

    // Walk the leaf chain collecting values in [minKey, maxKey]
    while (node) {
        for (size_t i = 0; i < node->keys.size(); ++i) {
            if (node->keys[i] > maxKey) return result;
            if (node->keys[i] >= minKey) result.push_back(node->values[i]);
        }
        node = node->next;
    }
    return result;
}

template<typename KeyType, typename ValueType>
std::vector<ValueType> BPlusTree<KeyType, ValueType>::getAllValues() const {
    if (!root) return {};

    // Walk down to the leftmost leaf
    auto node = root;
    while (!node->isLeaf) {
        node = node->children[0];
    }

    std::vector<ValueType> result;
    result.reserve(count);
    while (node) {
        result.insert(result.end(), node->values.begin(), node->values.end());
        node = node->next;
    }
    return result;
}

template<typename KeyType, typename ValueType>
void BPlusTree<KeyType, ValueType>::clear() {
    root  = std::make_shared<Node>(true);
    count = 0;
}

// ─── Explicit instantiations ─────────────────────────────────────────────────
template class BPlusTree<int64_t, int>;
template class BPlusTree<int64_t, size_t>;

} // namespace TSDB
