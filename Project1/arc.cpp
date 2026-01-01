#include "arc.h"

int ARCCache::get(int target) {
    if (_c <= 0) {
        return -1;
    }

    ++_get_count;

    auto it = _table.find(target);
    if (it != _table.end()) {
        // hit
        ++_hit_count;

        if (it->second.seg == Segment::T1) {
            // 命中在 T1：从 T1 提升到 T2 头部
            if (_t2_cap == 0) {
                // 极端小 cache：退化为单段 LRU（都在 T1）
                _t1.splice(_t1.begin(), _t1, it->second.it);
                it->second.it = _t1.begin();
                return target;
            }

            _t2.splice(_t2.begin(), _t1, it->second.it);
            it->second.seg = Segment::T2;
            it->second.it = _t2.begin();

            // T2 超过容量：尾部降级到 T1 头部
            while (_t2.size() > _t2_cap) {
                int demote = _t2.back();
                _t2.pop_back();
                _t1.push_front(demote);
                auto& info = _table[demote];
                info.seg = Segment::T1;
                info.it = _t1.begin();
            }

            // T1 超过容量：尾部淘汰
            while (_t1.size() > _t1_cap) {
                int evict = _t1.back();
                _t1.pop_back();
                _table.erase(evict);
            }

            return target;
        }

        // 命中在 T2：移动到 T2 头部
        _t2.splice(_t2.begin(), _t2, it->second.it);
        it->second.it = _t2.begin();
        return target;
    }

    // miss
    ++_miss_count;

    // 插入到 T1 头部
    _t1.push_front(target);
    _table.emplace(target, NodeInfo{ Segment::T1, _t1.begin() });

    // T1 超过容量：尾部淘汰
    while (_t1.size() > _t1_cap) {
        int evict = _t1.back();
        _t1.pop_back();
        _table.erase(evict);
    }

    return target;
}

std::string ARCCache::statics() {
    std::stringstream s;
    s << "trace:" << _file_name << " arc_cache:"
        << " cache_size:" << _c
        << " request:" << _get_count
        << " hit:" << _hit_count
        << " miss:" << _miss_count  // miss count
        << " hit_rate:" << 1.0 * _hit_count / _get_count << std::endl;
    return s.str();
}