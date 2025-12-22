#include "iscore.h"
#include <iostream>
#include <cmath>

namespace {
// 与 SCORE 同步的温度重标定（时间粒度：每个 block 访问递增一次 now_req）
constexpr double kTempLambda = 1e-3;     // 半衰期约 693 次访问
constexpr double kTempAdd = 1.0;
constexpr double kDensityScale = 1.0;
}

// 计算给定对象的 ISCORE 分数（含 interval weight）
double ISCORECache::calculateScoreWithInterval(int block_id, uint32_t now_req) {
    auto meta_it = _meta.find(block_id);
    if (meta_it == _meta.end()) {
        return 0.0;
    }

    const auto& meta = meta_it->second;
    double age = static_cast<double>(now_req - meta.last_req);
    if (age < 1.0) age = 1.0;

    // 计算温度密度（与 SCORE 一致）
    double density = meta.temperature / (static_cast<double>(meta.size) * age);

    // 计算重要性
    double importance = std::log1p(static_cast<double>(meta.freq));

    // 原始 SCORE 分数
    double score_base = importance + density * kDensityScale;

    // 计算 interval 权重（eviction-only）
    uint64_t rid = regionId(block_id);
    double weight = wInterval(rid);

    return score_base * weight;
}

int ISCORECache::get(const ISCOREParams& params) {
    if (_c <= 0) {
        return -1;
    }

    ++_get_count;

    // 使用 SCORE 中同样的逻辑时间：这里直接将 request_number 视为访问序号
    _access_seq = static_cast<uint64_t>(params.now_req);

    // 每次访问（命中或未命中）都要更新其所在 region 的 interval-hotness
    uint64_t rid_current = regionId(params.target);
    updateRegionHot(rid_current);

    auto it = _table.find(params.target);

    if (it != _table.end()) {
        // 命中：移动到链表头部
        ++_hit_count;
        _items.splice(_items.begin(), _items, it->second);

        // O(1) 更新元数据（保持与 SCORE 相同逻辑）
        auto& meta = _meta[params.target];
        double age = static_cast<double>(params.now_req - meta.last_req);
        if (age < 1.0) age = 1.0;

        meta.temperature = meta.temperature * std::exp(-kTempLambda * age) + kTempAdd;
        meta.last_req = params.now_req;
        meta.freq++;

        // 命中时：更新该对象在堆中的 score（每个 key 仅一条记录）
        double score_now = calculateScoreWithInterval(params.target, params.now_req);
        _victim_heap.upsert(params.target, score_now);

        return _items.front().second;
    }
    else {
        // 未命中
        if (_items.size() >= static_cast<size_t>(_c)) {
            // 缓存满：使用可更新最小堆选择 victim
            int victim = -1;
            const double EPS = 1e-6;

            // 通过“刷新堆顶直到稳定”的方式，保证淘汰按当前 now_req 的精确分数（包含 interval 权重）进行
            while (!_victim_heap.empty()) {
                const auto top = _victim_heap.top();

                // 安全检查：堆顶不在 cache，直接弹出（理论上不应发生，因为淘汰时会同步 erase）
                if (_table.find(top.key) == _table.end()) {
                    _victim_heap.pop();
                    continue;
                }

                double score_now = calculateScoreWithInterval(top.key, params.now_req);
                if (std::fabs(score_now - top.score) > EPS) {
                    _victim_heap.upsert(top.key, score_now);
                    continue;
                }

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
                _victim_heap.erase(victim);
            }
        }

        // 插入新对象（与 SCORE 相同逻辑）
        _items.emplace_front(params.target, params.target);
        _table[params.target] = _items.begin();

        ISMeta new_meta;
        new_meta.temperature = kTempAdd;
        new_meta.last_req = params.now_req;
        new_meta.freq = 1;
        // trace 中每个 block 为 512 bytes
        new_meta.size = params.size_of_blocks * 512;
        _meta[params.target] = new_meta;

        // 插入时：插入/更新堆 entry
        double score_initial = calculateScoreWithInterval(params.target, params.now_req);
        _victim_heap.upsert(params.target, score_initial);

        return _items.front().second;
    }
}

std::string ISCORECache::statics() {
    std::stringstream s;
    s << "trace:" << _file_name << " ISCORE_cache:"
        << " cache_size:" << _c
        << " request:" << _get_count
        << " hit:" << _hit_count
        << " hit_rate:" << 1.0 * _hit_count / _get_count << std::endl;
    return s.str();
}

uint64_t ISCORECache::regionId(int block_or_object_id) {
    // 当前 trace 中，SCORE 的 target 即为 block_id（来自 trace_line.starting_block+i）
    // 因此这里直接使用 block_id / REGION_SIZE_BLOCKS 把连续 block 划分为 region。
    if (_region_size_blocks == 0) {
        return 0;
    }
    return static_cast<uint64_t>(block_or_object_id) / _region_size_blocks;
}

void ISCORECache::updateRegionHot(uint64_t rid) {
    // 逻辑时间 _access_seq 单调递增（由 request_number 提供）
    uint64_t last_seq = 0;
    auto it_last = _region_last_seq.find(rid);
    if (it_last != _region_last_seq.end()) {
        last_seq = it_last->second;
    }

    double H = 0.0;
    auto it_hot = _region_hot.find(rid);
    if (it_hot != _region_hot.end()) {
        H = it_hot->second;
    }

    uint64_t dt = _access_seq - last_seq;
    double dt_d = static_cast<double>(dt);

    // H = H * exp(-LAMBDA * dt) + 1
    H = H * std::exp(-_lambda * dt_d) + 1.0;

    _region_hot[rid] = H;
    _region_last_seq[rid] = _access_seq;
}

double ISCORECache::wInterval(uint64_t rid) {
    double H = 0.0;
    uint64_t last_seq = 0;

    auto it_hot = _region_hot.find(rid);
    if (it_hot != _region_hot.end()) {
        H = it_hot->second;
    }
    auto it_last = _region_last_seq.find(rid);
    if (it_last != _region_last_seq.end()) {
        last_seq = it_last->second;
    }

    // 将 H 衰减到当前 _access_seq（这里不再 +1，因为这是 “观察” 而非 “访问更新”）
    uint64_t dt = _access_seq - last_seq;
    double dt_d = static_cast<double>(dt);
    H = H * std::exp(-_lambda * dt_d);

    double norm = 0.0;
    if (H + _smooth_c > 0.0) {
        norm = H / (H + _smooth_c);
    }

    return 1.0 + _alpha * norm;
}


