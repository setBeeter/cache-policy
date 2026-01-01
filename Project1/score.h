#pragma once

#include <unordered_map>
#include <list>
#include <vector>
#include <sstream>
#include <string>

#include "TraceLine.h"

struct TemperatureRecord {
    int object_id;
    double temperature;
    double related_temperature;
    double last_access_time;
};

struct SCOREParams {
    int target;  // 要访问的 block/object id
    const std::vector<trace_line>& trace_records; // 供淘汰阶段使用的 trace 记录
};

class SCORECache {
public:
    explicit SCORECache(int c, std::string file_name)
        : _c(c), _file_name(std::move(file_name)), _hit_count(0), _get_count(0) {}

    SCORECache(const SCORECache&) = delete;
    SCORECache& operator=(const SCORECache&) = delete;
    ~SCORECache() = default;

public:
    std::unordered_map<int, TemperatureRecord> calculateTemperature(const std::vector<trace_line>& trace_records);
    std::unordered_map<int, double> calculateDensity(const std::unordered_map<int, TemperatureRecord>& temperatureTable,
                                                     const std::vector<trace_line>& trace_records);
    std::unordered_map<int, double> calculateImportance(const std::vector<trace_line>& trace_records);
    std::unordered_map<int, double> normalizeImportanceTable(const std::unordered_map<int, double>& importance_table);
    std::unordered_map<int, double> calculateKdensityTable(const std::unordered_map<int, double>& densityTable);
    std::vector<int> evit(const std::unordered_map<int, double>& k_importance,
                          const std::unordered_map<int, double>& k_density);

    bool cache_full();
    int get(const SCOREParams& scoreparam);
    std::string statics();

private:
    std::list<std::pair<int, int>> _items;
    std::unordered_map<int, std::list<std::pair<int, int>>::iterator> _table;

    int _c;
    unsigned int _hit_count;
    unsigned int _get_count;
    std::string _file_name;
};
