#include "ihc.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cassert>

// ========== 计时全局变量（定义） ==========
double g_t_evict_ns = 0.0;
double g_t_evict_traverse_ns = 0.0;
uint64_t g_evict_count = 0;

// ========================================================================
// 构造函数
// ========================================================================

IHCCache::IHCCache(int c, std::string file_name,
                       int interval_len,
                       double alpha,
                       double th_up,
                       double th_down,
                       int step_rule)
    : _c(c),
      _p_hot(c / 2),  // 初始 p_hot = C/2
      _alpha(alpha),
      _th_up(th_up),
      _th_down(th_down),
      _step_rule(step_rule),
      _global_req_id(0),
      _global_interval_id(0),
      _interval_len(interval_len),
      _file_name(std::move(file_name))
{
    // 参数校验
    assert(_th_up > _th_down && "Th_up must be > Th_down for hysteresis");
    assert(_alpha > 0.0 && _alpha <= 1.0 && "Alpha must be in (0, 1]");
    assert(_interval_len > 0 && "Interval length must be > 0");

    if (_c > 0) {
        _hot_pos.reserve(static_cast<size_t>(_c) * 2 + 1);
        _warm_pos.reserve(static_cast<size_t>(_c) * 2 + 1);
        _g_hot_pos.reserve(static_cast<size_t>(_c) * 2 + 1);
        _g_warm_pos.reserve(static_cast<size_t>(_c) * 2 + 1);
        _heat_map.reserve(static_cast<size_t>(_c) * 2 + 1);
    }

    printConfig();
}

// ========================================================================
// 启动时打印配置
// ========================================================================

void IHCCache::printConfig() {
    std::cout << "\n========== IHC (Interval-aware Hot/Warm Cache) ==========\n";
    std::cout << "  Capacity (C):          " << _c << "\n";
    std::cout << "  Initial p_hot:         " << _p_hot << "\n";
    std::cout << "  Interval length:        " << _interval_len << "\n";
    std::cout << "  Alpha (EMA):            " << _alpha << "\n";
    std::cout << "  Th_up (promote):        " << _th_up << "\n";
    std::cout << "  Th_down (demote):       " << _th_down << "\n";
    std::cout << "  Step rule:              " << (_step_rule == 0 ? "fixed(1)" : "dynamic") << "\n";
    std::cout << "  Constraints:            |HOT|+|WARM| <= C, |G_hot|+|G_warm| <= C\n";
    std::cout << "==================================================================================\n\n";
}

// ========================================================================
// 辅助函数：从队列删除
// ========================================================================

void IHCCache::removeFromQueue(int obj_id, std::list<int>& queue,
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

void IHCCache::addToMRU(int obj_id, std::list<int>& queue,
                           std::unordered_map<int, std::list<int>::iterator>& pos_map) {
    // 确保对象不在队列中（先删除）
    removeFromQueue(obj_id, queue, pos_map);
    
    queue.push_front(obj_id);
    pos_map[obj_id] = queue.begin();
}

// ========================================================================
// 更新对象热度（懒更新，基于 interval gap）
// ========================================================================

void IHCCache::updateHeat(int obj_id) {
    auto& meta = _heat_map[obj_id];
    
    // 计算 interval gap
    int gap = _global_interval_id - meta.last_interval_id;
    
    if (gap > 0 && meta.last_interval_id >= 0) {
        // 懒更新：根据 gap 衰减热度
        // heat *= pow(alpha, gap)
        meta.heat *= std::pow(_alpha, gap);
    }
    
    // EMA 增量更新：heat = alpha*heat + (1-alpha)*1
    meta.heat = _alpha * meta.heat + (1.0 - _alpha) * 1.0;
    
    // 更新 last_interval_id
    meta.last_interval_id = _global_interval_id;
}

// ========================================================================
// Replace 函数：根据 p_hot 决定从 HOT 还是 WARM 淘汰
// ========================================================================

void IHCCache::replace() {
    int hot_size = static_cast<int>(_hot.size());
    int warm_size = static_cast<int>(_warm.size());
    int g_hot_size = static_cast<int>(_g_hot.size());
    int g_warm_size = static_cast<int>(_g_warm.size());
    
    // 如果 |HOT| > p_hot：从 HOT 淘汰
    if (hot_size > _p_hot && !_hot.empty()) {
        // 从 HOT LRU 移动到 G_hot MRU
        int victim = _hot.back();
        _hot.pop_back();
        _hot_pos.erase(victim);
        
        // G_hot 容量限制：|G_hot| + |G_warm| <= C
        if (g_hot_size + g_warm_size >= _c && !_g_hot.empty()) {
            // 如果 G_hot+G_warm 已满，删除 G_hot 的 LRU
            int old = _g_hot.back();
            _g_hot.pop_back();
            _g_hot_pos.erase(old);
        }
        _g_hot.push_front(victim);
        _g_hot_pos[victim] = _g_hot.begin();
        
        ++_evict_from_hot;
    } else if (!_warm.empty()) {
        // 否则：从 WARM 淘汰 WARM LRU 尾部
        int victim = _warm.back();
        _warm.pop_back();
        _warm_pos.erase(victim);
        
        // G_warm 容量限制：|G_hot| + |G_warm| <= C
        if (g_hot_size + g_warm_size >= _c && !_g_warm.empty()) {
            // 如果 G_hot+G_warm 已满，删除 G_warm 的 LRU
            int old = _g_warm.back();
            _g_warm.pop_back();
            _g_warm_pos.erase(old);
        }
        _g_warm.push_front(victim);
        _g_warm_pos[victim] = _g_warm.begin();
        
        ++_evict_from_warm;
    } else if (!_hot.empty() && hot_size + warm_size >= _c) {
        // 如果 WARM 为空但 HOT 不为空，且总容量已满，从 HOT 淘汰
        // （这种情况可能发生在所有对象都在 HOT 时）
        int victim = _hot.back();
        _hot.pop_back();
        _hot_pos.erase(victim);
        
        // G_hot 容量限制
        if (g_hot_size + g_warm_size >= _c && !_g_hot.empty()) {
            int old = _g_hot.back();
            _g_hot.pop_back();
            _g_hot_pos.erase(old);
        }
        _g_hot.push_front(victim);
        _g_hot_pos[victim] = _g_hot.begin();
        
        ++_evict_from_hot;
    }
    // 如果 HOT 和 WARM 都为空，不需要淘汰（这种情况不应该发生，但安全起见保留）
}

// ========================================================================
// 检查容量约束（断言）
// ========================================================================

void IHCCache::checkConstraints() const {
    int hot_size = static_cast<int>(_hot.size());
    int warm_size = static_cast<int>(_warm.size());
    int g_hot_size = static_cast<int>(_g_hot.size());
    int g_warm_size = static_cast<int>(_g_warm.size());
    
    assert(hot_size + warm_size <= _c);      // |HOT| + |WARM| <= C
    assert(g_hot_size + g_warm_size <= _c);  // |G_hot| + |G_warm| <= C
    assert(hot_size + warm_size + g_hot_size + g_warm_size <= 2 * _c);  // 总结构 <= 2C
}

// ========================================================================
// 主要 get 函数（IHC 逻辑）
// ========================================================================

int IHCCache::get(const IHCParams& params) {
    if (_c <= 0) {
        return -1;
    }

    ++_get_count;
    ++_global_req_id;
    
    // 更新 global_interval_id（懒更新，不做全量遍历）
    if (_global_req_id % _interval_len == 0) {
        ++_global_interval_id;
    }
    
    int x = params.target;

    // 检查 x 在哪个队列
    bool in_hot = (_hot_pos.find(x) != _hot_pos.end());
    bool in_warm = (_warm_pos.find(x) != _warm_pos.end());
    bool in_g_hot = (_g_hot_pos.find(x) != _g_hot_pos.end());
    bool in_g_warm = (_g_warm_pos.find(x) != _g_warm_pos.end());

    // 断言：对象不能同时存在于多个队列
    assert((in_hot ? 1 : 0) + (in_warm ? 1 : 0) + (in_g_hot ? 1 : 0) + (in_g_warm ? 1 : 0) <= 1);

    auto t0_evict = std::chrono::steady_clock::now();

    if (in_hot) {
        // ========== Case A: x in HOT (real hit) ==========
        ++_real_hit;
        ++_hot_hit;

        // 更新热度
        updateHeat(x);

        // 获取当前热度
        double heat_x = _heat_map[x].heat;

        // 检查是否需要降级到 WARM
        if (heat_x <= _th_down) {
            // 降级：从 HOT 移动到 WARM MRU
            removeFromQueue(x, _hot, _hot_pos);
            addToMRU(x, _warm, _warm_pos);
            ++_demote_hot_to_warm;
        } else {
            // 仍在 HOT，移动到 HOT MRU
            removeFromQueue(x, _hot, _hot_pos);
            addToMRU(x, _hot, _hot_pos);
        }
    }
    else if (in_warm) {
        // ========== Case B: x in WARM (real hit) ==========
        ++_real_hit;
        ++_warm_hit;

        // 更新热度
        updateHeat(x);

        // 获取当前热度
        double heat_x = _heat_map[x].heat;

        // 检查是否需要晋升到 HOT
        if (heat_x >= _th_up) {
            // 晋升：从 WARM 移动到 HOT MRU
            removeFromQueue(x, _warm, _warm_pos);
            addToMRU(x, _hot, _hot_pos);
            ++_promote_warm_to_hot;
        } else {
            // 仍在 WARM，移动到 WARM MRU
            removeFromQueue(x, _warm, _warm_pos);
            addToMRU(x, _warm, _warm_pos);
        }
    }
    else if (in_g_hot) {
        // ========== Case C: x in G_hot (ghost hit - 属于 MISS) ==========
        // ✓ Ghost hit 不算 cache hit（对象不在 HOT/WARM，需要从后端加载）
        assert(!in_hot && !in_warm);
        
        ++_miss;
        ++_ghost_hit_hot;

        // 自适应：p_hot = min(C, p_hot + step)
        int step = 1;
        if (_step_rule == 1) {
            // 动态比例：step = max(1, |G_warm|/max(1,|G_hot|))
            int g_hot_size = static_cast<int>(_g_hot.size());
            int g_warm_size = static_cast<int>(_g_warm.size());
            if (g_hot_size > 0) {
                step = std::max(1, g_warm_size / g_hot_size);
            }
        }
        _p_hot = std::min(_c, _p_hot + step);

        // 确保 HOT+WARM 有空间（在插入 x 之前）
        int hot_size = static_cast<int>(_hot.size());
        int warm_size = static_cast<int>(_warm.size());
        if (hot_size + warm_size >= _c) {
            replace();
        }

        // 把 x 插入 HOT MRU（因为这是"热对象被误淘汰"）
        removeFromQueue(x, _g_hot, _g_hot_pos);
        addToMRU(x, _hot, _hot_pos);
        
        // 更新热度（初始化或更新）
        updateHeat(x);
    }
    else if (in_g_warm) {
        // ========== Case D: x in G_warm (ghost hit - 属于 MISS) ==========
        // ✓ Ghost hit 不算 cache hit（对象不在 HOT/WARM，需要从后端加载）
        assert(!in_hot && !in_warm);
        
        ++_miss;
        ++_ghost_hit_warm;

        // 自适应：p_hot = max(0, p_hot - step)
        int step = 1;
        if (_step_rule == 1) {
            // 动态比例：step = max(1, |G_hot|/max(1,|G_warm|))
            int g_hot_size = static_cast<int>(_g_hot.size());
            int g_warm_size = static_cast<int>(_g_warm.size());
            if (g_warm_size > 0) {
                step = std::max(1, g_hot_size / g_warm_size);
            }
        }
        _p_hot = std::max(0, _p_hot - step);

        // 确保 HOT+WARM 有空间（在插入 x 之前）
        int hot_size = static_cast<int>(_hot.size());
        int warm_size = static_cast<int>(_warm.size());
        if (hot_size + warm_size >= _c) {
            replace();
        }

        // 把 x 插入 WARM MRU（或：根据 heat 门控决定插 HOT/WARM；默认 WARM）
        removeFromQueue(x, _g_warm, _g_warm_pos);
        
        // 更新热度（初始化或更新）
        updateHeat(x);
        double heat_x = _heat_map[x].heat;
        
        // 根据热度门控决定插入 HOT 还是 WARM
        if (heat_x >= _th_up) {
            addToMRU(x, _hot, _hot_pos);
            ++_promote_warm_to_hot;
        } else {
            addToMRU(x, _warm, _warm_pos);
        }
    }
    else {
        // ========== Case E: x is new (cold miss) ==========
        ++_miss;

        // 确保 HOT+WARM 有空间（在插入 x 之前）
        // 如果容量已满，需要先淘汰
        int hot_size = static_cast<int>(_hot.size());
        int warm_size = static_cast<int>(_warm.size());
        
        if (hot_size + warm_size >= _c) {
            replace();
        }

        // 插入 WARM MRU（新对象先入 WARM）
        addToMRU(x, _warm, _warm_pos);
        
        // 初始化/更新热度
        updateHeat(x);
    }

    auto t1_evict = std::chrono::steady_clock::now();
    extern double g_t_evict_ns;
    extern uint64_t g_evict_count;
    g_t_evict_ns += std::chrono::duration<double, std::nano>(t1_evict - t0_evict).count();
    ++g_evict_count;

    // 检查约束
    checkConstraints();

    // 可选：定期打印 p_hot
    if (_get_count % IHCConfig::P_PRINT_INTERVAL == 0) {
        std::cout << "[IHC] request=" << _get_count 
                  << " p_hot=" << _p_hot 
                  << " interval_id=" << _global_interval_id
                  << " HOT=" << _hot.size() 
                  << " WARM=" << _warm.size() << std::endl;
    }

    return params.target;
}

// ========================================================================
// 统计输出
// ========================================================================

std::string IHCCache::statics() {
    // 计算统计
    double real_hit_rate = (_get_count > 0) ? (1.0 * _real_hit / _get_count) : 0.0;
    double miss_rate = (_get_count > 0) ? (1.0 * _miss / _get_count) : 0.0;
    double ghost_hit_rate = (_get_count > 0) ? (1.0 * (_ghost_hit_hot + _ghost_hit_warm) / _get_count) : 0.0;

    std::stringstream s;
    s << "trace:" << _file_name << " IHC:"
      << " cache_size:" << _c
      << " request:" << _get_count
      << " real_hit:" << _real_hit
      << " real_hit_rate:" << real_hit_rate
      << " miss:" << _miss
      << " miss_rate:" << miss_rate
      << " final_p_hot:" << _p_hot
      << "\n";

    // 命中分布（明确区分 real hit 和 ghost hit）
    s << "  real_hit (HOT+WARM): " << _real_hit 
      << " (HOT:" << _hot_hit << " WARM:" << _warm_hit << ")"
      << "\n";
    s << "  ghost_hit (G_hot+G_warm): " << (_ghost_hit_hot + _ghost_hit_warm)
      << " (G_hot:" << _ghost_hit_hot << " G_warm:" << _ghost_hit_warm << ")"
      << " ghost_rate:" << ghost_hit_rate
      << "\n";

    // 晋升/降级统计
    s << "  promotion: WARM->HOT:" << _promote_warm_to_hot
      << " demotion: HOT->WARM:" << _demote_hot_to_warm
      << "\n";

    // 淘汰来源
    s << "  eviction: from_HOT:" << _evict_from_hot
      << " from_WARM:" << _evict_from_warm
      << "\n";

    // 队列占用情况
    s << "  queue_usage: HOT:" << _hot.size()
      << " WARM:" << _warm.size()
      << " G_hot:" << _g_hot.size()
      << " G_warm:" << _g_warm.size()
      << " total:" << (_hot.size() + _warm.size() + _g_hot.size() + _g_warm.size())
      << "\n";

    // 参数信息
    s << "  params: interval_len=" << _interval_len
      << " alpha=" << _alpha
      << " Th_up=" << _th_up
      << " Th_down=" << _th_down
      << " step_rule=" << (_step_rule == 0 ? "fixed" : "dynamic")
      << "\n";

    return s.str();
}

