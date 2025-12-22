#include "score.h"
#include <iostream>
#include <cmath>

namespace {
// 以 “每个 block 访问递增一次 now_req” 为时间粒度时，原先 exp(-0.5*age) 衰减过于激进，
// 会让温度快速归零，算法退化成近似 LFU，命中率往往显著劣于 LRU。
// 这里重标定温度更新，使其在该粒度下仍然能体现“最近性”。
constexpr double kTempLambda = 1e-3;     // 温度指数衰减系数；半衰期约 693 次访问
constexpr double kTempAdd = 1.0;         // 每次访问增加的温度增量
constexpr double kDensityScale = 1.0;    // 密度项缩放（原先 *1000 在新温度尺度下容易过大）
}

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
    // 使用 log1p 限制“老热点频次累积”对 score 的碾压效应，更符合缓存的短期局部性
    double importance = std::log1p(static_cast<double>(meta.freq));

    // 计算综合得分（与原版完全一致）
    return importance + density * kDensityScale;
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
        
        // 温度指数衰减后增加 kTempAdd（按每-block 时间粒度重标定）
        meta.temperature = meta.temperature * std::exp(-kTempLambda * age) + kTempAdd;
        meta.last_req = params.now_req;
        meta.freq++;

        // 命中时：更新该对象在堆中的 score（每个 key 仅一条记录）
        double score_now = calculateScore(params.target, params.now_req);
        _victim_heap.upsert(params.target, score_now);

        return _items.front().second;
    }
    else {
        // 未命中
        if (_items.size() >= static_cast<size_t>(_c)) {
            // 缓存满：使用可更新最小堆选择 victim
            int victim = -1;
            const double EPS = 1e-6;  // score 比较阈值

            // 通过“刷新堆顶直到稳定”的方式，保证淘汰按当前 now_req 的精确分数进行（不牺牲命中率语义）
            while (!_victim_heap.empty()) {
                const auto top = _victim_heap.top();

                // 安全检查：堆顶不在 cache，直接弹出（理论上不应发生，因为淘汰时会同步 erase）
                if (_table.find(top.key) == _table.end()) {
                    _victim_heap.pop();
                    continue;
                }

                double score_now = calculateScore(top.key, params.now_req);
                if (std::fabs(score_now - top.score) > EPS) {
                    // 分数随时间推进发生变化：更新该 key 的 score 并重新堆化
                    _victim_heap.upsert(top.key, score_now);
                    continue;
                }

                // 堆顶已是当前时刻的最小分对象
                victim = top.key;
                _victim_heap.pop();
                break;
            }

            // 淘汰 victim
            if (victim != -1) {
                auto victim_it = _table.find(victim);
                if (victim_it != _table.end()) {
                    _items.erase(victim_it->second);
                    _table.erase(victim_it);
                    _meta.erase(victim);
                }
                // 同步从堆中移除（若已 pop 则 no-op）
                _victim_heap.erase(victim);
            }
        }

        // 插入新对象
        _items.emplace_front(params.target, params.target);
        _table[params.target] = _items.begin();

        // 初始化元数据
        Meta new_meta;
        new_meta.temperature = kTempAdd;
        new_meta.last_req = params.now_req;
        new_meta.freq = 1;
        // trace 中每个 block 为 512 bytes
        new_meta.size = params.size_of_blocks * 512;
        _meta[params.target] = new_meta;

        // 插入时：插入/更新堆 entry
        double score_initial = calculateScore(params.target, params.now_req);
        _victim_heap.upsert(params.target, score_initial);

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
