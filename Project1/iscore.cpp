#include "iscore.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <limits>
#include <cassert>

// ========== 计时全局变量 ==========
double g_t_evict_ns = 0.0;
double g_t_evict_traverse_ns = 0.0;
uint64_t g_evict_count = 0;

// ========================================================================
// 构造函数
// ========================================================================

ISCORECache::ISCORECache(int c, std::string file_name,
                         double lambda, double c_smooth, int evict_k)
    : _c(c),
      _p(c / 2),  // 初始 p = C/2
      _hit_count(0),
      _get_count(0),
      _file_name(std::move(file_name)),
      _lambda(lambda),
      _c_smooth(c_smooth),
      _evict_k(evict_k)
{
    if (_c > 0) {
        _t1_pos.reserve(static_cast<size_t>(_c) * 2 + 1);
        _t2_pos.reserve(static_cast<size_t>(_c) * 2 + 1);
        _b1_pos.reserve(static_cast<size_t>(_c) * 2 + 1);
        _b2_pos.reserve(static_cast<size_t>(_c) * 2 + 1);
    }

    printConfig();
}

// ========================================================================
// 启动时打印配置
// ========================================================================

void ISCORECache::printConfig() {
    std::cout << "\n========== ARC (Adaptive Replacement Cache) ==========\n";
    std::cout << "  Capacity (C):          " << _c << "\n";
    std::cout << "  Initial p:             " << _p << "\n";
    std::cout << "  Constraints:           |T1|+|T2| <= C, |B1|+|B2| <= C\n";
    std::cout << "========================================================\n\n";
}

// ========================================================================
// 辅助函数：从队列删除
// ========================================================================

void ISCORECache::removeFromQueue(int obj_id, std::list<int>& queue,
                                   std::unordered_map<int, std::list<int>::iterator>& pos_map) {
    auto it = pos_map.find(obj_id);
    if (it != pos_map.end()) {
        queue.erase(it->second);
        pos_map.erase(it);
    }
}

// ========================================================================
// 辅助函数：添加到 MRU（头部）
// ========================================================================

void ISCORECache::addToMRU(int obj_id, std::list<int>& queue,
                            std::unordered_map<int, std::list<int>::iterator>& pos_map) {
    // 确保对象不在队列中（先删除）
    removeFromQueue(obj_id, queue, pos_map);
    
    queue.push_front(obj_id);
    pos_map[obj_id] = queue.begin();
}

// ========================================================================
// Replace 函数：根据 p 决定从 T1 还是 T2 淘汰
// ========================================================================

void ISCORECache::replace() {
    int t1_size = static_cast<int>(_t1.size());
    int b1_size = static_cast<int>(_b1.size());
    int b2_size = static_cast<int>(_b2.size());
    
    // ARC Replace 逻辑：如果 |T1| >= 1 且 (|T1| > p 或 (|T1| == p 且 T2 为空))
    // 从 T1 淘汰；否则从 T2 淘汰
    if (t1_size > 0 && (t1_size > _p || (t1_size == _p && _t2.empty()))) {
        // 从 T1 LRU 移动到 B1 MRU
        int victim = _t1.back();
        _t1.pop_back();
        _t1_pos.erase(victim);
        
        // B1 容量限制：|B1| + |B2| <= C
        if (b1_size + b2_size >= _c && !_b1.empty()) {
            // 如果 B1+B2 已满，删除 B1 的 LRU
            int old = _b1.back();
            _b1.pop_back();
            _b1_pos.erase(old);
        }
        _b1.push_front(victim);
        _b1_pos[victim] = _b1.begin();
        
        ++_evict_from_t1;
    } else {
        // 从 T2 LRU 移动到 B2 MRU
        if (!_t2.empty()) {
            int victim = _t2.back();
            _t2.pop_back();
            _t2_pos.erase(victim);
            
            // B2 容量限制：|B1| + |B2| <= C
            if (b1_size + b2_size >= _c && !_b2.empty()) {
                // 如果 B1+B2 已满，删除 B2 的 LRU
                int old = _b2.back();
                _b2.pop_back();
                _b2_pos.erase(old);
            }
            _b2.push_front(victim);
            _b2_pos[victim] = _b2.begin();
            
            ++_evict_from_t2;
        }
    }
}

// ========================================================================
// 检查容量约束（断言）
// ========================================================================

void ISCORECache::checkConstraints() const {
    int t1_size = static_cast<int>(_t1.size());
    int t2_size = static_cast<int>(_t2.size());
    int b1_size = static_cast<int>(_b1.size());
    int b2_size = static_cast<int>(_b2.size());
    
    assert(t1_size + t2_size <= _c);  // |T1| + |T2| <= C
    assert(b1_size + b2_size <= _c);  // |B1| + |B2| <= C
    assert(t1_size + t2_size + b1_size + b2_size <= 2 * _c);  // 总结构 <= 2C
}

// ========================================================================
// 主要 get 函数（标准 ARC 逻辑）
// ========================================================================

int ISCORECache::get(const ISCOREParams& params) {
    if (_c <= 0) {
        return -1;
    }

    ++_get_count;
    int x = params.target;

    // 检查 x 在哪个队列
    bool in_t1 = (_t1_pos.find(x) != _t1_pos.end());
    bool in_t2 = (_t2_pos.find(x) != _t2_pos.end());
    bool in_b1 = (_b1_pos.find(x) != _b1_pos.end());
    bool in_b2 = (_b2_pos.find(x) != _b2_pos.end());

    // 断言：对象不能同时存在于多个队列
    assert((in_t1 ? 1 : 0) + (in_t2 ? 1 : 0) + (in_b1 ? 1 : 0) + (in_b2 ? 1 : 0) <= 1);

    auto t0_evict = std::chrono::steady_clock::now();

    if (in_t1 || in_t2) {
        // ========== Case I: x in T1 or T2 (hit) ==========
        ++_hit_count;

        if (in_t1) {
            // x in T1: move from T1 to MRU of T2
            removeFromQueue(x, _t1, _t1_pos);
            addToMRU(x, _t2, _t2_pos);
            ++_t1_hit;
        } else {
            // x in T2: move to MRU of T2
            removeFromQueue(x, _t2, _t2_pos);
            addToMRU(x, _t2, _t2_pos);
            ++_t2_hit;
        }
    }
    else if (in_b1) {
        // ========== Case II: x in B1 (ghost hit - 属于 MISS) ==========
        // ✓ Ghost hit 不算 cache hit（对象不在 T1/T2，需要从后端加载）
        // ✓ 断言：B1 命中必须发生在 cache miss 路径（x 不在 T1/T2）
        assert(!in_t1 && !in_t2);
        
        ++_b1_hit;

        // 调整 p: p = min(C, p + max(1, |B2|/|B1|))
        int b1_size = static_cast<int>(_b1.size());
        int b2_size = static_cast<int>(_b2.size());
        if (b1_size > 0) {
            int delta = std::max(1, b2_size / b1_size);
            _p = std::min(_c, _p + delta);
        }

        // Replace
        replace();

        // move x from B1 to MRU of T2
        removeFromQueue(x, _b1, _b1_pos);
        addToMRU(x, _t2, _t2_pos);
    }
    else if (in_b2) {
        // ========== Case III: x in B2 (ghost hit - 属于 MISS) ==========
        // ✓ Ghost hit 不算 cache hit（对象不在 T1/T2，需要从后端加载）
        // ✓ 断言：B2 命中必须发生在 cache miss 路径（x 不在 T1/T2）
        assert(!in_t1 && !in_t2);
        
        ++_b2_hit;

        // 调整 p: p = max(0, p - max(1, |B1|/|B2|))
        int b1_size = static_cast<int>(_b1.size());
        int b2_size = static_cast<int>(_b2.size());
        if (b2_size > 0) {
            int delta = std::max(1, b1_size / b2_size);
            _p = std::max(0, _p - delta);
        }

        // Replace
        replace();

        // move x from B2 to MRU of T2
        removeFromQueue(x, _b2, _b2_pos);
        addToMRU(x, _t2, _t2_pos);
    }
    else {
        // ========== Case IV: x is new (miss) ==========

        int t1_size = static_cast<int>(_t1.size());
        int t2_size = static_cast<int>(_t2.size());
        int b1_size = static_cast<int>(_b1.size());
        int b2_size = static_cast<int>(_b2.size());
        int total = t1_size + t2_size + b1_size + b2_size;

        if (t1_size + b1_size == _c) {
            // if |T1| + |B1| == C:
            if (t1_size < _c) {
                // if |T1| < C: evict LRU from B1, then Replace
                if (!_b1.empty()) {
                    int old = _b1.back();
                    _b1.pop_back();
                    _b1_pos.erase(old);
                }
                replace();
            } else {
                // else (|T1| == C): evict LRU from T1 (move to B1)
                if (!_t1.empty()) {
                    int victim = _t1.back();
                    _t1.pop_back();
                    _t1_pos.erase(victim);
                    
                    // B1 容量限制：如果 B1+B2 >= C，需要删除 B1 的 LRU
                    if (b1_size + b2_size >= _c && !_b1.empty()) {
                        int old = _b1.back();
                        _b1.pop_back();
                        _b1_pos.erase(old);
                    }
                    _b1.push_front(victim);
                    _b1_pos[victim] = _b1.begin();
                    ++_evict_from_t1;
                }
            }
        } else if (t1_size + t2_size + b1_size + b2_size >= _c) {
            // else if |T1|+|T2|+|B1|+|B2| >= C:
            if (total >= 2 * _c) {
                // if total >= 2C: evict LRU from B2
                if (!_b2.empty()) {
                    int old = _b2.back();
                    _b2.pop_back();
                    _b2_pos.erase(old);
                }
            }
            replace();
        }

        // 确保 T1+T2 有空间（在插入 x 之前）
        if (static_cast<int>(_t1.size()) + static_cast<int>(_t2.size()) >= _c) {
            replace();
        }

        // insert x into MRU of T1
        addToMRU(x, _t1, _t1_pos);
        ++g_evict_count;
    }

    auto t1_evict = std::chrono::steady_clock::now();
    g_t_evict_ns += std::chrono::duration<double, std::nano>(t1_evict - t0_evict).count();

    // 检查约束
    checkConstraints();

    // 可选：定期打印 p
    if (_get_count % ARCConfig::P_PRINT_INTERVAL == 0) {
        std::cout << "[ARC] request=" << _get_count << " p=" << _p << std::endl;
    }

    return params.target;
}

// ========================================================================
// 统计输出
// ========================================================================

std::string ISCORECache::statics() {
    // 计算统计
    uint64_t real_hit_cnt = _t1_hit + _t2_hit;       // 真命中（T1+T2）
    uint64_t ghost_hit_cnt = _b1_hit + _b2_hit;      // ghost 命中（属于 miss）
    double real_hit_rate = (_get_count > 0) ? (1.0 * real_hit_cnt / _get_count) : 0.0;
    double ghost_hit_rate = (_get_count > 0) ? (1.0 * ghost_hit_cnt / _get_count) : 0.0;

    std::stringstream s;
    s << "trace:" << _file_name << " ARC:"
      << " cache_size:" << _c
      << " request:" << _get_count
      << " real_hit:" << real_hit_cnt
      << " real_hit_rate:" << real_hit_rate
      << " final_p:" << _p
      << "\n";

    // 命中分布（明确区分 real hit 和 ghost hit）
    s << "  real_hit (T1+T2): " << real_hit_cnt 
      << " (T1:" << _t1_hit << " T2:" << _t2_hit << ")"
      << "\n";
    s << "  ghost_hit (B1+B2): " << ghost_hit_cnt
      << " (B1:" << _b1_hit << " B2:" << _b2_hit << ")"
      << " ghost_rate:" << ghost_hit_rate
      << "\n";

    // 淘汰来源
    s << "  eviction: from_T1:" << _evict_from_t1
      << " from_T2:" << _evict_from_t2
      << "\n";

    // 队列占用情况
    s << "  queue_usage: T1:" << _t1.size()
      << " T2:" << _t2.size()
      << " B1:" << _b1.size()
      << " B2:" << _b2.size()
      << " total:" << (_t1.size() + _t2.size() + _b1.size() + _b2.size())
      << "\n";

    return s.str();
}
