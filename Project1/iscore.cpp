#include "iscore.h"
#include <iostream>
#include <cmath>
#include <limits>

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
    double importance = static_cast<double>(meta.freq);

    // 原始 SCORE 分数
    double score_base = importance + density * 1000.0;

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

        meta.temperature = meta.temperature * std::exp(-0.5 * age) + 1000.0;
        meta.last_req = params.now_req;
        meta.freq++;

        // 命中时：更新版本号并 push 新 heap entry
        _version[params.target]++;
        double score_now = calculateScoreWithInterval(params.target, params.now_req);
        _victim_heap.push({score_now, params.target, _version[params.target]});

        return _items.front().second;
    }
    else {
        // 未命中
        if (_items.size() >= static_cast<size_t>(_c)) {
            // 缓存满，使用 Lazy Heap 选择 victim（O(log N) 均摊复杂度）
            int victim = -1;
            const double EPS = 1e-6;

            // Lazy cleaning：循环直到找到真正的 victim
            while (!_victim_heap.empty()) {
                auto top = _victim_heap.top();
                _victim_heap.pop();

                // 检查 1：对象已不在 cache
                if (_table.find(top.key) == _table.end()) {
                    continue;
                }

                // 检查 2：版本过期
                if (_version.find(top.key) != _version.end() && top.version != _version[top.key]) {
                    continue;
                }

                // 检查 3：score 过期（时间推进或 region hotness 变化）
                double score_now = calculateScoreWithInterval(top.key, params.now_req);
                if (std::fabs(score_now - top.score_snapshot) > EPS) {
                    // score 已变化，重新 push
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
                    _version.erase(victim);
                }
            }
        }

        // 插入新对象（与 SCORE 相同逻辑）
        _items.emplace_front(params.target, params.target);
        _table[params.target] = _items.begin();

        ISMeta new_meta;
        new_meta.temperature = 1000.0;
        new_meta.last_req = params.now_req;
        new_meta.freq = 1;
        new_meta.size = params.size_of_blocks * 4096;
        _meta[params.target] = new_meta;

        // 插入时：初始化版本号并 push heap entry
        _version[params.target] = 1;
        double score_initial = calculateScoreWithInterval(params.target, params.now_req);
        _victim_heap.push({score_initial, params.target, 1});

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
    if (REGION_SIZE_BLOCKS == 0) {
        return 0;
    }
    return static_cast<uint64_t>(block_or_object_id) / REGION_SIZE_BLOCKS;
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
    H = H * std::exp(-LAMBDA * dt_d) + 1.0;

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
    H = H * std::exp(-LAMBDA * dt_d);

    double norm = 0.0;
    if (H + C > 0.0) {
        norm = H / (H + C);
    }

    return 1.0 + ALPHA * norm;
}


