#include "TDC.h"
#include <iostream>
#include <chrono>
int TDCCache::get(const TDCParams& params) {
    if (_capacity <= 0) {
        return -1;
    }
    ++_get_count;
    auto it = _table.find(params.target);

    if (it != _table.end()) {
        // 命中：目标对象已经在缓存中
        ++_hit_count;
        _items.splice(_items.begin(), _items, it->second);

        // 计算当前周期的温度（指数衰减：周期数越大温度越低）
        double temperature = 1000000.0 * pow(0.5, params.n);
        // 获取当前时间
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

        // 记录温度相关信息
        auto duration_since_epoch = now.time_since_epoch();

        // 更新温度表
        temperatureTable[params.target][params.n].temperature = temperature; // 当前周期温度
        temperatureTable[params.target][params.n].size = params.size;        // 对象大小
        temperatureTable[params.target][params.n].last_access_time = now;

        return _items.front().second;
    }
    else {
        // 未命中：对象不在缓存中
        if (_items.size() >= _capacity) {
            // 缓存已满：需要选择一批对象进行淘汰
            std::vector<int> objectsToEvict;

            // 为缓存中的每个对象计算温度密度，并填入密度表
            std::unordered_map<int, double> densityTable;
            for (const auto& item : _items) {
                int objectId = item.first;
                const std::unordered_map<int, temp>& targetTemperatureTable = temperatureTable[objectId];
                // 检查该对象在当前周期是否有温度记录
                auto targetIt = targetTemperatureTable.find(params.n);
                if (targetIt != targetTemperatureTable.end()) {
                    double calculateAge(const std::chrono::system_clock::time_point& lastAccessTime);
                    double age = calculateAge(targetIt->second.last_access_time);
                    double temperatureDensity = targetIt->second.temperature / (targetIt->second.size * age);
                    densityTable[objectId] = temperatureDensity;
                }
            }

            // 计算平均温度密度
            double totalDensity = 0.0;
            for (const auto& entry : densityTable) {
                totalDensity += entry.second;
            }
            double averageDensity = totalDensity / densityTable.size();

            // 选择需要淘汰的对象：温度密度低于平均值
            for (const auto& entry : densityTable) {
                int objectId = entry.first;
                double density = entry.second;
                if (density < averageDensity) {
                    objectsToEvict.push_back(objectId);
                }
            }

            // 执行淘汰：从链表和哈希表中移除对象
            for (int objectId : objectsToEvict) {
                auto tableIt = _table.find(objectId);
                if (tableIt == _table.end()) {
                    continue;
                }
                _items.erase(tableIt->second);
                _table.erase(tableIt);
            }
        }
        // 未命中且缓存未满：先将温度初始化为 0
        double temperature = 0.0;
        // 如果存在历史周期（n > 1），则累加历史温度
        if (params.n > 1) {
            // 遍历 1 ~ n-1 周期对应的温度记录，累加历史温度
            std::unordered_map<int, temp>& historyTemperatures = temperatureTable[params.target];
            for (int i = 1; i < params.n; ++i) {
                // 检查每个周期是否有记录
                auto periodIt = historyTemperatures.find(i);
                if (periodIt != historyTemperatures.end()) {
                    // 累加历史温度
                    temperature += periodIt->second.temperature;
                }
            }
        }
        // 获取当前时间
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

        // 将当前周期的温度写入温度表
        auto duration_since_epoch = now.time_since_epoch();
        // 更新温度表
        temperatureTable[params.target][params.n].temperature = temperature; // 当前周期温度
        temperatureTable[params.target][params.n].size = params.size;        // 对象大小
        temperatureTable[params.target][params.n].last_access_time = now;

        // 将新对象插入缓存（链表头部）
        _items.emplace_front(params.target, params.target);
        _table[params.target] = _items.begin();

        return _items.front().second;
    }
}

// 计算对象"年龄"：从上次访问到当前的时间间隔（秒）
double calculateAge(const std::chrono::system_clock::time_point& lastAccessTime) {
    // 获取当前时间
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    auto duration_since_epoch = now - lastAccessTime;
    double ageInSeconds = std::chrono::duration_cast<std::chrono::seconds>(duration_since_epoch).count();

    // 返回以秒为单位的年龄，用于计算温度密度
    return ageInSeconds;
}
std::string TDCCache::statics() {
    std::stringstream s;
    s << "trace:" << _file_name << " TDC_cache:"
        << " cache_size:" << _capacity
        << " request:" << _get_count
        << " hit:" << _hit_count
        << " hit_rate:" << 1.0 * _hit_count / _get_count << std::endl;
    return s.str();
}
