#include "score.h"
#include <iostream>
#include <cmath>
#include <limits>

// 计算给定对象在指定时间点的 SCORE 分数
double SCORECache::calculateScore(int block_id, uint32_t now_req) {
    auto meta_it = _meta.find(block_id);
    if (meta_it == _meta.end()) {
        return 0.0;  // 对象不在 cache，返回 0
    }

    const auto& meta = meta_it->second;
    double age = static_cast<double>(now_req - meta.last_req);
    if (age < 1.0) age = 1.0;  // 避免除零

    // 计算温度密度
    double density = meta.temperature / (static_cast<double>(meta.size) * age);

    // 计算重要性
    double importance = static_cast<double>(meta.freq);

    // 计算综合得分（与原版完全一致）
    return importance + density * 1000.0;
}

// 旧版函数已删除，新版 get() 使用在线维护的元数据
int SCORECache::get(const SCOREParams& params) {
    if (_c <= 0) {
        return -1;
    }

    ++_get_count;
    auto it = _table.find(params.target);

    if (it != _table.end()) {
        // 命中：移动到链表头部
        ++_hit_count;
        _items.splice(_items.begin(), _items, it->second);

        // O(1) 更新元数据
        auto& meta = _meta[params.target];
        double age = static_cast<double>(params.now_req - meta.last_req);
        if (age < 1.0) age = 1.0;  // 防止除零或负值
        
        // 温度指数衰减后增加 1000.0
        meta.temperature = meta.temperature * std::exp(-0.5 * age) + 1000.0;
        meta.last_req = params.now_req;
        meta.freq++;

        // 命中时：更新版本号并 push 新的 heap entry（lazy）
        _version[params.target]++;
        double score_now = calculateScore(params.target, params.now_req);
        _victim_heap.push({score_now, params.target, _version[params.target]});

        return _items.front().second;
    }
    else {
        // 未命中
        if (_items.size() >= static_cast<size_t>(_c)) {
            // 缓存满，使用 Lazy Heap 选择 victim（O(log N) 均摊复杂度）
            int victim = -1;
            const double EPS = 1e-6;  // score 比较阈值

            // Lazy cleaning：循环直到找到真正的 victim
            while (!_victim_heap.empty()) {
                auto top = _victim_heap.top();
                _victim_heap.pop();

                // 检查 1：对象已不在 cache（之前被淘汰）
                if (_table.find(top.key) == _table.end()) {
                    continue;
                }

                // 检查 2：版本过期（对象被更新过）
                if (_version.find(top.key) != _version.end() && top.version != _version[top.key]) {
                    continue;
                }

                // 检查 3：score 过期（时间推进导致 score 变化）
                double score_now = calculateScore(top.key, params.now_req);
                if (std::fabs(score_now - top.score_snapshot) > EPS) {
                    // score 已变化，重新 push 到 heap
                    _version[top.key]++;
                    _victim_heap.push({score_now, top.key, _version[top.key]});
                    continue;
                }

                // 找到真正的 victim
                victim = top.key;
                break;
            }

            // 淘汰 victim
            if (victim != -1) {
                auto victim_it = _table.find(victim);
                if (victim_it != _table.end()) {
                    _items.erase(victim_it->second);
                    _table.erase(victim_it);
                    _meta.erase(victim);
                    _version.erase(victim);  // 清理版本号
                }
            }
        }

        // 插入新对象
        _items.emplace_front(params.target, params.target);
        _table[params.target] = _items.begin();

        // 初始化元数据
        Meta new_meta;
        new_meta.temperature = 1000.0;
        new_meta.last_req = params.now_req;
        new_meta.freq = 1;
        new_meta.size = params.size_of_blocks * 4096;
        _meta[params.target] = new_meta;

        // 插入时：初始化版本号并 push heap entry
        _version[params.target] = 1;
        double score_initial = calculateScore(params.target, params.now_req);
        _victim_heap.push({score_initial, params.target, 1});

        return _items.front().second;
    }
}
std::string SCORECache::statics() {
    std::stringstream s;
    s << "trace:" << _file_name << " score_cache:"
        << " cache_size:" << _c
        << " request:" << _get_count
        << " hit:" << _hit_count
        << " hit_rate:" << 1.0 * _hit_count / _get_count << std::endl;
    return s.str();
}
