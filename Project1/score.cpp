#include "score.h"
#include <iostream>
#include <chrono>
#include "TraceLine.h"
#include <thread>
#include <random>



// 温度计算函数
std::unordered_map<int, TemperatureRecord> SCORECache::calculateTemperature(const std::vector<trace_line>& trace_records) {
    const double k = 0.5;

    std::unordered_map<int, TemperatureRecord> temperatureTable;
    // 初始化随机数生成器和分布
    std::mt19937 gen(std::random_device{}()); // Standard mersenne_twister_engine seeded with random_device
    std::uniform_real_distribution<> dis(1.0, 100.0); // 分布在 1.0 到 100.0 之间
    // 获取系统当前时间
    auto getCurrentTime = []() {
        return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count());
        };
    double current_time = getCurrentTime(); // 获取一个系统时间
    for (size_t i = 0; i < trace_records.size(); ++i) {
        const auto& trace = trace_records[i];
        // 查找对象ID是否存在
        auto it = temperatureTable.find(trace.request_number);

        if (it == temperatureTable.end()) {
            // 如果是新对象，计算温度并添加到温度表
            double E = 1000.0;
            double current_access_time = static_cast<double>(trace.current_time); // 从trace_records中获取时间
            double new_temperature = E;

            // 添加当前时间到温度表
            temperatureTable[trace.request_number] = {
                trace.request_number, new_temperature, 0.0,  current_access_time
            };

        }
        else {
            // 如果是已存在对象，计算温度并更新温度表
            double E = 0.0;
            // 计算新的访问时间
            double last_access_time = current_time+ dis(gen); // 使用相同的当前时间
            double age = last_access_time - it->second.last_access_time;
            double new_temperature = it->second.temperature * std::exp(-k * age);
            it->second.temperature = new_temperature;
            it->second.last_access_time = last_access_time;
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

            // 更新并设置当前对象的访问时间
            it->second.last_access_time = getCurrentTime();

        }
    }
    // 在函数内部返回 temperature_table
    return temperatureTable;
}
// 计算温度密度函数
std::unordered_map<int, double>  SCORECache::calculateDensity(const std::unordered_map<int, TemperatureRecord>& temperatureTable, const std::vector<trace_line>& trace_records) {
    std::unordered_map<int, double> densityTable;
    // 获取系统当前时间


    auto getCurrentTime = []() {
        return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count());
        };
    // 用于遍历 trace_records 向量，每次处理一个 std::vector 中的元素
    for (size_t i = 0; i < trace_records.size(); ++i) {
        const auto& trace = trace_records[i];

        // 查找对象ID是否存在
        auto it = temperatureTable.find(trace.request_number);

        if (it != temperatureTable.end()) {
            // 获取块的大小，块大小为 4096
            int block_size = 4096;
            double CurrentTime = getCurrentTime();
            // 计算对象的大小
            double object_size = trace.size_of_blocks * block_size;

            // 计算年龄
            double age = CurrentTime - it->second.last_access_time;

            // 计算温度密度
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
        double normalized_value = (entry.second - min_val) / (max_val - min_val);

        // 计算 k_density 并存储到哈希表
        k_density[entry.first] = entry.second * normalized_value;
        // 打印每个对象的密度值和 k_density
        //std::cout << "Object ID: " << entry.first << ", Density: " << entry.second << ", k_density: " << k_density[entry.first] << std::endl;
    }

    return k_density;
}
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
        if (_items.size() >= _c) {
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
                auto it = _table.find(obj_id);
                if (it != _table.end()) {
                    _items.erase(it->second);
                    _table.erase(it);
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
// 获取当前时间的函数
double getCurrentTime() {
    return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count());
}
