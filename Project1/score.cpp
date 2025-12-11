#include "score.h"
#include <iostream>
#include <limits>
#include "TraceLine.h"



// 温度计算函数（已优化：去除随机数和系统时间调用，使用trace中的时间）
std::unordered_map<int, TemperatureRecord> SCORECache::calculateTemperature(const std::vector<trace_line>& trace_records) {
    const double k = 0.5;

    std::unordered_map<int, TemperatureRecord> temperatureTable;
    
    // 优化：不再使用随机数和系统时间，直接使用 trace 中的时间或索引
    for (size_t i = 0; i < trace_records.size(); ++i) {
        const auto& trace = trace_records[i];
        // 查找对象ID是否存在
        auto it = temperatureTable.find(trace.request_number);

        if (it == temperatureTable.end()) {
            // 如果是新对象，计算温度并添加到温度表
            double E = 1000.0;
            // 优化：使用 trace.current_time 作为访问时间，不再使用系统时间
            double current_access_time = static_cast<double>(trace.current_time);
            double new_temperature = E;

            // 添加到温度表
            temperatureTable[trace.request_number] = {
                trace.request_number, new_temperature, 0.0, current_access_time
            };

        }
        else {
            // 如果是已存在对象，计算温度并更新温度表
            // 优化：使用 trace.current_time 作为访问时间，不再使用系统时间和随机数
            double current_access_time = static_cast<double>(trace.current_time);
            double age = current_access_time - it->second.last_access_time;
            // 保持原有的指数衰减公式
            double new_temperature = it->second.temperature * std::exp(-k * age);
            it->second.temperature = new_temperature;
            it->second.last_access_time = current_access_time;
            
            // 计算前一个对象的温度
            if (i > 0) {
                auto prevIt = temperatureTable.find(trace_records[i - 1].request_number);
                if (prevIt != temperatureTable.end()) {
                    double related_temperature = it->second.temperature +
                        (it->second.temperature - prevIt->second.temperature) * std::exp(-k);
                    prevIt->second.related_temperature = related_temperature;
                }
            }

            // 更新当前对象的关联温度
            it->second.related_temperature = temperatureTable[trace.request_number].temperature;
        }
    }
    // 在函数内部返回 temperature_table
    return temperatureTable;
}
// 计算温度密度函数（已优化：去除系统时间调用，使用trace中的时间）
std::unordered_map<int, double>  SCORECache::calculateDensity(const std::unordered_map<int, TemperatureRecord>& temperatureTable, const std::vector<trace_line>& trace_records) {
    std::unordered_map<int, double> densityTable;
    
    // 优化：找到所有trace中的最大时间作为"当前时间"（预处理时的终点时间）
    double max_time = 0.0;
    for (const auto& trace : trace_records) {
        max_time = std::max(max_time, static_cast<double>(trace.current_time));
    }
    
    // 用于遍历 trace_records 向量，每次处理一个 std::vector 中的元素
    for (size_t i = 0; i < trace_records.size(); ++i) {
        const auto& trace = trace_records[i];

        // 查找对象ID是否存在
        auto it = temperatureTable.find(trace.request_number);

        if (it != temperatureTable.end()) {
            // 获取块的大小，块大小为 4096
            int block_size = 4096;
            // 优化：使用 max_time 作为当前时间，不再使用系统时间
            // 计算对象的大小
            double object_size = trace.size_of_blocks * block_size;

            // 计算年龄（从最后访问时间到trace序列结束的时间差）
            double age = max_time - it->second.last_access_time;
            
            // 避免除零：如果 age 或 object_size 为 0，给一个小的默认值
            if (age <= 0.0) age = 1.0;
            if (object_size <= 0.0) object_size = block_size;

            // 计算温度密度（保持原有公式）
            double density = 10 * it->second.temperature / (object_size * age);

            // 添加到密度哈希表
            densityTable[trace.request_number] = density;
        }
    }
    return densityTable;
}

std::unordered_map<int, double>  SCORECache::calculateImportance(const std::vector<trace_line>& trace_records) {
    std::unordered_map<int, double> importance_table;

    // 优化：只遍历一次，对于每个 request_number，如果是第一次遇到，记录它的 access_count
    // 这保持了原逻辑（取第一次出现的 access_count），但复杂度从 O(n²) 降为 O(n)
    for (size_t i = 0; i < trace_records.size(); ++i) {
        const auto& trace = trace_records[i];

        // 优化：如果这个 request_number 还没有记录过，记录它
        // 原逻辑中 std::find_if 总是找到第一个匹配的，所以等价于第一次遇到时记录
        // 复杂度从 O(n²) 降为 O(n)
        if (importance_table.find(trace.request_number) == importance_table.end()) {
            // 直接使用当前 trace 的 access_count（等价于原逻辑中 std::find_if 找到的第一个）
            double importance = static_cast<double>(trace.access_count);

            importance_table[trace.request_number] = importance;
        }
        // 如果已经记录过，跳过（原逻辑中会重复覆盖相同的值，所以跳过不影响结果）
    }
    return importance_table;
}
std::unordered_map<int, double>  SCORECache::normalizeImportanceTable(const std::unordered_map<int, double>& importance_table) {
    std::unordered_map<int, double> k_important;

    // 如果 importance_table 为空，直接返回空表
    if (importance_table.empty()) {
        return k_important;
    }

    // 找到最小值和最大值
    double min_val = std::numeric_limits<double>::max();
    double max_val = std::numeric_limits<double>::min();

    for (const auto& entry : importance_table) {
        min_val = std::min(min_val, entry.second);
        max_val = std::max(max_val, entry.second);
    }

    // 计算每个重要性的归一化值，将归一化值与原始重要性相乘，存储到 k_important 中
    for (const auto& entry : importance_table) {
        if (max_val != min_val) {
            double normalized_value = (entry.second - min_val) / (max_val - min_val);
            k_important[entry.first] = entry.second * normalized_value;
            // 打印每个对象的 k_important
            //std::cout << "Object ID: " << entry.first << ", k_important: " << k_important[entry.first] << std::endl;
        }
        else {
            // 如果所有值的最小值相等的对象，设置为0
            k_important[entry.first] = 0.0;
            // 打印每个对象的 k_important
            //std::cout << "Object ID: " << entry.first << ", k_important: " << k_important[entry.first] << std::endl;
        }
    }

    return k_important;

}
std::unordered_map<int, double> SCORECache::calculateKdensityTable(const std::unordered_map<int, double>& densityTable) {
    std::unordered_map<int, double> k_density;

    // 如果 densityTable 为空，直接返回空表
    if (densityTable.empty()) {
        return k_density;
    }

    // 找到最小值和最大值
    double min_val = std::numeric_limits<double>::max();
    double max_val = std::numeric_limits<double>::min();

    for (const auto& entry : densityTable) {
        min_val = std::min(min_val, entry.second);
        max_val = std::max(max_val, entry.second);
    }
    // 打印最小值和最大值
    //std::cout << "Min Value: " << min_val << ", Max Value: " << max_val << std::endl;
    // 对每个值进行归一化，减去最小值
    for (const auto& entry : densityTable) {
        if (max_val != min_val) {
            double normalized_value = (entry.second - min_val) / (max_val - min_val);
            // 计算 k_density 并存储到哈希表
            k_density[entry.first] = entry.second * normalized_value;
            // 打印每个对象的密度值和 k_density
            //std::cout << "Object ID: " << entry.first << ", Density: " << entry.second << ", k_density: " << k_density[entry.first] << std::endl;
        }
        else {
            // 如果所有值相等，设置为0
            k_density[entry.first] = 0.0;
            // 打印每个对象的 k_density
            //std::cout << "Object ID: " << entry.first << ", k_density: " << k_density[entry.first] << std::endl;
        }
    }

    return k_density;
}

// NOTE: This function is no longer used in the refactored SCORE cache.
// It is kept only for reference and compatibility.
std::vector<int> SCORECache::evit(const std::unordered_map<int, double>& k_importance, const std::unordered_map<int, double>& k_density) {
    // 计算每个对象的 score
    std::unordered_map<int, double> score_table;

    for (const auto& entry : k_importance) {
        if (k_density.find(entry.first) != k_density.end()) {
            double score = k_importance.at(entry.first) + k_density.at(entry.first);
            score_table[entry.first] = score;
        }
    }

    // 计算平均值
    double avg_score = 0.0;
    for (const auto& entry : score_table) {
        avg_score += entry.second;
    }
    avg_score /= score_table.size();

    // 淘汰低于平均值的对象
    std::vector<int> to_remove;
    for (const auto& entry : score_table) {
        if (entry.second < avg_score) {
            to_remove.push_back(entry.first);
        }
    }

    return to_remove;
}
bool SCORECache::cache_full()
{
    return _items.size() >= _c;
}

// 预计算所有对象的SCORE得分（一次性计算，避免每次淘汰时重复全量计算）
void SCORECache::precompute_scores(const std::vector<trace_line>& trace_records) {
    // 1. 计算温度表
    std::unordered_map<int, TemperatureRecord> temperatureTable = calculateTemperature(trace_records);
    
    // 2. 计算密度表
    std::unordered_map<int, double> densityTable = calculateDensity(temperatureTable, trace_records);
    
    // 3. 计算重要性表
    std::unordered_map<int, double> importance_table = calculateImportance(trace_records);
    
    // 4. 归一化密度表，得到 k_density
    std::unordered_map<int, double> k_density = calculateKdensityTable(densityTable);
    
    // 5. 归一化重要性表，得到 k_importance
    std::unordered_map<int, double> k_important = normalizeImportanceTable(importance_table);
    
    // 6. 计算每个对象的最终得分：score = k_importance + k_density
    //    并存储到 _precomputedScore
    _precomputedScore.clear();
    
    // 遍历 k_important（或 k_density，应该包含相同的对象ID集合）
    for (const auto& entry : k_important) {
        int obj_id = entry.first;
        // k_importance 部分（entry 本身已经是 k_important 的元素）
        double score = entry.second;
        
        // k_density 部分（只查找一次）
        auto den_it = k_density.find(obj_id);
        if (den_it != k_density.end()) {
            score += den_it->second;
        }
        
        // 存储预计算的得分
        _precomputedScore[obj_id] = score;
    }
    
    // 标记预计算已完成
    _scoresReady = true;
}
int SCORECache::get(const SCOREParams& scoreparam) {

    if (_c <= 0) {
        return -1;
    }

    // 懒加载：第一次调用时，如果预计算尚未完成，则执行一次预计算
    if (!_scoresReady) {
        precompute_scores(scoreparam.trace_records);
    }

    ++_get_count;
    auto it = _table.find(scoreparam.target); //查找trcaline访问的块号是不是在缓存中
    if (it != _table.end()) {
        // 缓存命中，使用LRU策略移到前端
        ++_hit_count;
        _items.splice(_items.begin(), _items, it->second);
        return _items.front().second;
    }
    else {
        // 缓存未命中
        if (_items.size() >= _c) {
            // 如果缓存已满，执行淘汰算法
            // 优化：不再对整条 trace_records 做全量计算，而是只从当前缓存对象中选择 victim
            
            int victim_id = -1;
            double min_score = std::numeric_limits<double>::max();
            
            // 遍历当前缓存中的所有对象
            for (const auto& item : _items) {
                int obj_id = item.first;
                double score = 0.0;
                
                // 在预计算的得分表中查找该对象的得分
                auto score_it = _precomputedScore.find(obj_id);
                if (score_it != _precomputedScore.end()) {
                    score = score_it->second;
                } else {
                    // 如果在预计算表中找不到（理论上不应该发生，但为了健壮性）
                    // 给一个默认的低分，这样更容易被淘汰
                    score = 0.0;
                }
                
                // 选出得分最低的对象作为 victim
                if (score < min_score) {
                    min_score = score;
                    victim_id = obj_id;
                }
            }
            
            // 淘汰得分最低的对象
            if (victim_id != -1) {
                auto victim_it = _table.find(victim_id);
                if (victim_it != _table.end()) {
                    _items.erase(victim_it->second);
                    _table.erase(victim_it);
                }
            }
        }
        
        // 在 _items 和 _table 中添加新对象
        _items.emplace_front(scoreparam.target, scoreparam.target);
        _table[scoreparam.target] = _items.begin();
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
