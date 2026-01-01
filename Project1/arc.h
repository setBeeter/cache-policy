#ifndef ARC_H
#define ARC_H

#include <cstddef>
#include <cstdint>
#include <list>
#include <sstream>
#include <string>
#include <unordered_map>

/**
 * @brief 高性能 SLRU（Segmented LRU）实现：作为原 ARC 的快速替代 baseline
 *
 * 目标：
 * - cache_size >= 1e6、百万级 trace 下极致性能
 * - 所有操作 O(1)：list splice + unordered_map 句柄
 * - 不使用 shared_ptr / new / make_shared
 *
 * 策略（工程近似，命中率允许明显下降）：
 * - 两段 LRU：T1（cold/probation，约 20%）+ T2（hot/protected，约 80%）
 * - miss：插入 T1 头
 * - hit in T1：提升到 T2 头
 * - hit in T2：移动到 T2 头
 * - T2 超过容量：T2 尾部降级到 T1 头
 * - T1 超过容量：T1 尾部淘汰
 */
class ARCCache {
public:
    explicit ARCCache(int c, std::string file_name)
        : _c(c),
          _file_name(std::move(file_name)),
          _hit_count(0),
          _get_count(0),
          _miss_count(0) {
        if (_c > 0) {
            // 固定分段比例：T1=20%，T2=80%
            _t1_cap = static_cast<std::size_t>(_c) * 2 / 10;
            if (_t1_cap == 0) _t1_cap = 1;
            _t2_cap = static_cast<std::size_t>(_c) - _t1_cap;

            _table.reserve(static_cast<std::size_t>(_c) * 2 + 1);
            _table.max_load_factor(0.7f);
    }
    }

    ARCCache(const ARCCache&) = delete;
    ARCCache& operator=(const ARCCache&) = delete;
    ~ARCCache() = default;

public:
    int get(int target);
    std::string statics();

private:
    enum class Segment : uint8_t { T1, T2 };
    struct NodeInfo {
        Segment seg;
        std::list<int>::iterator it;
    };

private:
    // 两段 LRU
    std::list<int> _t1;  // cold/probation
    std::list<int> _t2;  // hot/protected

    // key -> (segment, iterator)
    std::unordered_map<int, NodeInfo> _table;

    int _c;
    std::size_t _t1_cap{0};
    std::size_t _t2_cap{0};

    unsigned int _hit_count;
    unsigned int _get_count;
    unsigned int _miss_count;

    std::string _file_name;
};

#endif  // ARC_H
