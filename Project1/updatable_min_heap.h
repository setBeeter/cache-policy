#pragma once

#include <cassert>
#include <cstddef>
#include <unordered_map>
#include <vector>

/**
 * @brief 可更新的最小堆（每个 key 在堆内最多一个节点）
 *
 * 设计目的：
 * - 替代 priority_queue + lazy stale entry 的写法，避免堆体积随访问次数膨胀
 * - 支持 O(logN) 的 upsert / erase，并通过 key->index 句柄表定位元素
 *
 * 注意：
 * - 比较规则：先按 score 升序；score 相等时按 key 升序，保证稳定性
 */
class UpdatableMinHeap {
public:
    struct Entry {
        double score = 0.0;
        int key = 0;
    };

    void reserve(std::size_t n) {
        _heap.reserve(n);
        // 句柄表通常比堆稍大一些，减少 rehash
        _pos.reserve(n * 2 + 1);
    }

    bool empty() const { return _heap.empty(); }
    std::size_t size() const { return _heap.size(); }

    bool contains(int key) const {
        return _pos.find(key) != _pos.end();
    }

    const Entry& top() const { return _heap.front(); }

    void clear() {
        _heap.clear();
        _pos.clear();
    }

    void upsert(int key, double score) {
        auto it = _pos.find(key);
        if (it == _pos.end()) {
            _heap.push_back({ score, key });
            std::size_t idx = _heap.size() - 1;
            _pos[key] = idx;
            siftUp(idx);
            return;
        }

        std::size_t idx = it->second;
        // 句柄表不应指向越界位置
        assert(idx < _heap.size());
        _heap[idx].score = score;
        fix(idx);
    }

    void erase(int key) {
        auto it = _pos.find(key);
        if (it == _pos.end()) {
            return;
        }
        eraseAt(it->second);
    }

    void pop() {
        if (_heap.empty()) return;
        eraseAt(0);
    }

private:
    std::vector<Entry> _heap;
    std::unordered_map<int, std::size_t> _pos; // key -> heap index

private:
    static bool less(const Entry& a, const Entry& b) {
        if (a.score < b.score) return true;
        if (a.score > b.score) return false;
        return a.key < b.key;
    }

    void swapIdx(std::size_t i, std::size_t j) {
        auto tmp = _heap[i];
        _heap[i] = _heap[j];
        _heap[j] = tmp;
        _pos[_heap[i].key] = i;
        _pos[_heap[j].key] = j;
    }

    void siftUp(std::size_t idx) {
        while (idx > 0) {
            std::size_t parent = (idx - 1) / 2;
            if (!less(_heap[idx], _heap[parent])) {
                break;
            }
            swapIdx(idx, parent);
            idx = parent;
        }
    }

    void siftDown(std::size_t idx) {
        std::size_t n = _heap.size();
        while (true) {
            std::size_t left = idx * 2 + 1;
            if (left >= n) break;

            std::size_t right = left + 1;
            std::size_t best = left;
            if (right < n && less(_heap[right], _heap[left])) {
                best = right;
            }

            if (!less(_heap[best], _heap[idx])) {
                break;
            }

            swapIdx(idx, best);
            idx = best;
        }
    }

    void fix(std::size_t idx) {
        if (idx == 0) {
            siftDown(idx);
            return;
        }
        std::size_t parent = (idx - 1) / 2;
        if (less(_heap[idx], _heap[parent])) {
            siftUp(idx);
        }
        else {
            siftDown(idx);
        }
    }

    void eraseAt(std::size_t idx) {
        assert(idx < _heap.size());
        std::size_t last = _heap.size() - 1;
        const int key_to_remove = _heap[idx].key;

        if (idx != last) {
            // 先 swap（会更新两边 key 的位置），再 erase 掉被删除 key 的句柄，避免“删除后又被 swapIdx 写回 _pos”
            swapIdx(idx, last);
        }

        _heap.pop_back();
        _pos.erase(key_to_remove);

        if (idx < _heap.size()) {
            fix(idx);
        }
    }

    // 重度一致性校验（O(N)）：仅用于调试堆实现正确性。
    // 注意：不要在热路径每次调用，否则会把整体复杂度从 O(logN) 放大到 O(N)。
    void debugValidate() const {
#ifndef NDEBUG
        assert(_pos.size() == _heap.size());
        for (std::size_t i = 0; i < _heap.size(); ++i) {
            auto it = _pos.find(_heap[i].key);
            assert(it != _pos.end());
            assert(it->second == i);
        }
#endif
    }
};


