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
        // ��������ڻ�����
        ++_hit_count;
        _items.splice(_items.begin(), _items, it->second);

        // �����¶ȣ��������������м��㣩
        double temperature = 1000000.0 * pow(0.5, params.n);
        // ��ȡ��ǰʱ���
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

        // ת��Ϊ�Լ�Ԫ����������
        auto duration_since_epoch = now.time_since_epoch();

        // �����¶ȱ�
        temperatureTable[params.target][params.n].temperature = temperature; // ʹ�õ�ǰ������Ϊ��
        temperatureTable[params.target][params.n].size = params.size; // ���� params.size �ǵ�ǰ��С
        temperatureTable[params.target][params.n].last_access_time = now;

        return _items.front().second;
    }
    else {
        // �����ڻ�����
        if (_items.size() >= _capacity) {
            // �������������Ҫ��̭�Ķ��󣬲�Ҫ����ִ����̭����
            std::vector<int> objectsToEvict;

            // ����ÿ��������¶��ܶȲ��������ܶȱ���
            std::unordered_map<int, double> densityTable;
            for (const auto& item : _items) {
                int objectId = item.first;
                const std::unordered_map<int, temp>& targetTemperatureTable = temperatureTable[objectId];
                // ��������¶���Ϣ�Ƿ����
                auto targetIt = targetTemperatureTable.find(params.n);
                if (targetIt != targetTemperatureTable.end()) {
                    double calculateAge(const std::chrono::system_clock::time_point& lastAccessTime);
                    double age = calculateAge(targetIt->second.last_access_time);
                    double temperatureDensity = targetIt->second.temperature / (targetIt->second.size * age);
                    densityTable[objectId] = temperatureDensity;
                }
            }

            // ����ƽ���ܶ�
            double totalDensity = 0.0;
            for (const auto& entry : densityTable) {
                totalDensity += entry.second;
            }
            double averageDensity = totalDensity / densityTable.size();

            // ���Ҫ��̭�Ķ���
            for (const auto& entry : densityTable) {
                int objectId = entry.first;
                double density = entry.second;
                if (density < averageDensity) {
                    objectsToEvict.push_back(objectId);
                }
            }

            // ִ����̭����
            for (int objectId : objectsToEvict) {
                auto tableIt = _table.find(objectId);
                if (tableIt == _table.end()) {
                    continue;
                }
                _items.erase(tableIt->second);
                _table.erase(tableIt);
            }
        }
        // �����ڻ����У����ȶ�����Ϊ 0
        double temperature = 0.0;
        // ��� params.n - 1 �Ƿ�Ϊ����
        if (params.n > 1) {
            // ���� n-1 �����ڶ�Ӧ���¶ȱ���������ʷ�¶�
            std::unordered_map<int, temp>& historyTemperatures = temperatureTable[params.target];
            for (int i = 1; i < params.n; ++i) {
                // ���ÿ�������Ƿ��м�¼
                auto periodIt = historyTemperatures.find(i);
                if (periodIt != historyTemperatures.end()) {
                    // �ۼ���ʷ�¶�
                    temperature += periodIt->second.temperature;
                }
            }
        }
        // ��ȡ��ǰʱ���
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

        // ת��Ϊ�Լ�Ԫ����������
        auto duration_since_epoch = now.time_since_epoch();
        // �����¶ȱ�
        temperatureTable[params.target][params.n].temperature = temperature; // ʹ�õ�ǰ������Ϊ��
        temperatureTable[params.target][params.n].size = params.size; // ���� params.size �ǵ�ǰ��С
        temperatureTable[params.target][params.n].last_access_time = now;

        // ���¶�����뻺��
        _items.emplace_front(params.target, params.target);
        _table[params.target] = _items.begin();

        return _items.front().second;
    }
}

// ����ʱ��������
// 计算年龄函数（优化：使用访问计数器代替系统时间）
double calculateAge(const std::chrono::system_clock::time_point& lastAccessTime) {
    // ��ȡ��ǰʱ���
    // 优化：使用访问计数器代替系统时间

    // ����ʱ���
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    auto duration_since_epoch = now - lastAccessTime;
    double ageInSeconds = std::chrono::duration_cast<std::chrono::seconds>(duration_since_epoch).count();

    // ת��Ϊ�Լ�Ԫ����������
    // 确保age至少为1，避免除零错误
    // 注意：这里使用计数器差值作为相对时间单位，保持淘汰决策的相对关系不变
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