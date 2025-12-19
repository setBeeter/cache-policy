#include "score.h"
#include <iostream>
#include <cmath>
#include <limits>

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

        return _items.front().second;
    }
    else {
        // 未命中
        if (_items.size() >= _c) {
            // 缓存满，选择一个 victim 淘汰（O(cache_size) 扫描）
            int victim = -1;
            double min_score = std::numeric_limits<double>::max();

            // 遍历缓存中的所有对象计算 score
            for (const auto& item : _items) {
                int block_id = item.first;
                auto meta_it = _meta.find(block_id);
                if (meta_it == _meta.end()) continue;

                const auto& meta = meta_it->second;
                double age = static_cast<double>(params.now_req - meta.last_req);
                if (age < 1.0) age = 1.0;  // 避免除零

                // 计算温度密度
                double density = meta.temperature / (static_cast<double>(meta.size) * age);
                
                // 计算重要性（简化版：直接用频次，不做全局归一化）
                double importance = static_cast<double>(meta.freq);

                // 计算综合得分
                double score = importance + density * 1000.0;  // 放大密度权重使其与频次可比

                if (score < min_score) {
                    min_score = score;
                    victim = block_id;
                }
            }

            // 淘汰 victim
            if (victim != -1) {
                auto victim_it = _table.find(victim);
                if (victim_it != _table.end()) {
                    _items.erase(victim_it->second);
                    _table.erase(victim_it);
                    _meta.erase(victim);
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
