#include "score.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <random>
#include <cmath>
#include <limits>
#include <algorithm>

// 温度计算函数
std::unordered_map<int, TemperatureRecord> SCORECache::calculateTemperature(const std::vector<trace_line>& trace_records) {
    const double k = 0.5;

    std::unordered_map<int, TemperatureRecord> temperatureTable;

    // 初始化随机数生成器和分布
    std::mt19937 gen(std::random_device{}());
    std::uniform_real_distribution<> dis(1.0, 100.0);

    // 获取系统当前时间
    auto getCurrentTime = []() {
        return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count());
    };

    double current_time = getCurrentTime(); // 获取一个系统时间

    for (size_t i = 0; i < trace_records.size(); ++i) {
        const auto& trace = trace_records[i];
        int block_id = trace.starting_block;  // 使用 block_id（块粒度）

        // 查找对象ID是否存在
        auto it = temperatureTable.find(block_id);

        if (it == temperatureTable.end()) {
            // 如果是新对象，计算温度并添加到温度表
            double E = 1000.0;
            double current_access_time = static_cast<double>(trace.current_time);
            double new_temperature = E;

            temperatureTable[block_id] = {
                block_id, new_temperature, 0.0, current_access_time
            };
        }
        else {
            // 如果是已存在对象，计算温度并更新温度表
            double E = 0.0;
            (void)E;

            // 计算新的访问时间
            double last_access_time = current_time + dis(gen);
            double age = last_access_time - it->second.last_access_time;
            double new_temperature = it->second.temperature * std::exp(-k * age);
            it->second.temperature = new_temperature;
            it->second.last_access_time = last_access_time;

            // 计算前一个对象的温度
            if (i > 0) {
                int prev_block_id = trace_records[i - 1].starting_block;
                auto prevIt = temperatureTable.find(prev_block_id);
                if (prevIt != temperatureTable.end()) {
                    double related_temperature = it->second.temperature +
                        (it->second.temperature - prevIt->second.temperature) * std::exp(-k);
                    prevIt->second.related_temperature = related_temperature;
                }
            }

            // 更新当前对象的关联温度
            it->second.related_temperature = temperatureTable[block_id].temperature;

            // 更新并设置当前对象的访问时间
            it->second.last_access_time = getCurrentTime();
        }
    }
    return temperatureTable;
}

// 计算温度密度函数
std::unordered_map<int, double> SCORECache::calculateDensity(
    const std::unordered_map<int, TemperatureRecord>& temperatureTable,
    const std::vector<trace_line>& trace_records) {

    std::unordered_map<int, double> densityTable;

    auto getCurrentTime = []() {
        return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count());
    };

    for (size_t i = 0; i < trace_records.size(); ++i) {
        const auto& trace = trace_records[i];
        int block_id = trace.starting_block;  // 使用 block_id（块粒度）

        auto it = temperatureTable.find(block_id);

        if (it != temperatureTable.end()) {
            int block_size = 4096;
            double CurrentTime = getCurrentTime();

            double object_size = trace.size_of_blocks * block_size;

            double age = CurrentTime - it->second.last_access_time;

            double density = 10 * it->second.temperature / (object_size * age);

            densityTable[block_id] = density;
        }
    }
    return densityTable;
}

std::unordered_map<int, double> SCORECache::calculateImportance(const std::vector<trace_line>& trace_records) {
    std::unordered_map<int, double> importance_table;

    for (size_t i = 0; i < trace_records.size(); ++i) {
        const auto& trace = trace_records[i];
        int block_id = trace.starting_block;  // 使用 block_id（块粒度）

        if (importance_table.find(block_id) == importance_table.end()) {
            double importance = static_cast<double>(trace.access_count);
            importance_table[block_id] = importance;
        }
    }
    return importance_table;
}

std::unordered_map<int, double> SCORECache::normalizeImportanceTable(const std::unordered_map<int, double>& importance_table) {
    std::unordered_map<int, double> k_important;

    if (importance_table.empty()) {
        return k_important;
    }

    double min_val = std::numeric_limits<double>::max();
    double max_val = std::numeric_limits<double>::min();

    for (const auto& entry : importance_table) {
        min_val = std::min(min_val, entry.second);
        max_val = std::max(max_val, entry.second);
    }

    for (const auto& entry : importance_table) {
        if (max_val != min_val) {
            double normalized_value = (entry.second - min_val) / (max_val - min_val);
            k_important[entry.first] = entry.second * normalized_value;
        }
        else {
            k_important[entry.first] = 0.0;
        }
    }

    return k_important;
}

std::unordered_map<int, double> SCORECache::calculateKdensityTable(const std::unordered_map<int, double>& densityTable) {
    std::unordered_map<int, double> k_density;

    if (densityTable.empty()) {
        return k_density;
    }

    double min_val = std::numeric_limits<double>::max();
    double max_val = std::numeric_limits<double>::min();

    for (const auto& entry : densityTable) {
        min_val = std::min(min_val, entry.second);
        max_val = std::max(max_val, entry.second);
    }

    for (const auto& entry : densityTable) {
        double normalized_value = (entry.second - min_val) / (max_val - min_val);
        k_density[entry.first] = entry.second * normalized_value;
    }

    return k_density;
}

std::vector<int> SCORECache::evit(const std::unordered_map<int, double>& k_importance, const std::unordered_map<int, double>& k_density) {
    std::unordered_map<int, double> score_table;

    for (const auto& entry : k_importance) {
        if (k_density.find(entry.first) != k_density.end()) {
            double score = k_importance.at(entry.first) + k_density.at(entry.first);
            score_table[entry.first] = score;
        }
    }

    double avg_score = 0.0;
    for (const auto& entry : score_table) {
        avg_score += entry.second;
    }
    if (!score_table.empty()) {
        avg_score /= score_table.size();
    }

    std::vector<int> to_remove;
    for (const auto& entry : score_table) {
        if (entry.second < avg_score) {
            to_remove.push_back(entry.first);
        }
    }

    return to_remove;
}

bool SCORECache::cache_full() {
    return _items.size() >= static_cast<size_t>(_c);
}

int SCORECache::get(const SCOREParams& scoreparam) {
    if (_c <= 0) {
        return -1;
    }

    ++_get_count;
    auto it = _table.find(scoreparam.target);
    if (it != _table.end()) {
        ++_hit_count;
        _items.splice(_items.begin(), _items, it->second);
        return _items.front().second;
    }
    else {
        if (_items.size() >= static_cast<size_t>(_c)) {
            // 如果缓存已满，执行淘汰算法
            const std::vector<trace_line>& trace_records = scoreparam.trace_records;
            std::unordered_map<int, TemperatureRecord> temperatureTable = calculateTemperature(trace_records);
            std::unordered_map<int, double> densityTable = calculateDensity(temperatureTable, trace_records);

            std::unordered_map<int, double> importance_table = calculateImportance(trace_records);
            std::unordered_map<int, double> k_density = calculateKdensityTable(densityTable);
            std::unordered_map<int, double> k_important = normalizeImportanceTable(importance_table);
            auto to_remove = evit(k_important, k_density);

            // 从 _items 和 _table 中删除被淘汰的对象
            for (int obj_id : to_remove) {
                auto it2 = _table.find(obj_id);
                if (it2 != _table.end()) {
                    _items.erase(it2->second);
                    _table.erase(it2);
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
