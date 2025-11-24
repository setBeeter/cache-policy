#include <unordered_map>
#include <list>
#include <sstream>
#include <cstdint>
#include "TraceLine.h"
// 结构体或类，表示温度表的元素
struct TemperatureRecord {
    int object_id;
    double temperature;  // 将 temperature 类型修改为 double
    double related_temperature;// 新增，用于记录上一个对象的温度
    double last_access_time;  // 将 last_access_time 类型修改为 double
};

struct  SCOREParams {
    int target;
    const std::vector<trace_line>& trace_records;

};

class SCORECache {

public:
    explicit  SCORECache(int c, std::string file_name) :
        _c(c), _file_name(file_name), _hit_count(0), _get_count(0) {}
    //int get(const  resultTable& params);
    SCORECache(const  SCORECache&) = delete;
    SCORECache& operator=(const  SCORECache&) = delete;

    ~SCORECache() {}

public:
    int get(const SCOREParams& scoreparam);
    std::string statics();
    std::unordered_map<int, TemperatureRecord> calculateTemperature(const std::vector<trace_line>& trace_records);
    std::unordered_map<int, double> calculateDensity(const std::unordered_map<int, TemperatureRecord>& temperatureTable, const std::vector<trace_line>& trace_records);
    // 新添加的函数声明
    std::unordered_map<int, double> calculateImportance(const std::vector<trace_line>& trace_records);
    //void normalizeTable(std::unordered_map<int, double>& table);
    std::unordered_map<int, double> normalizeImportanceTable(const std::unordered_map<int, double>& importance_table);
    //std::unordered_map<int, double> calculateDensityAndImportance(const std::vector<trace_line>& trace_records, const std::unordered_map<int, TemperatureRecord>& temperatureTable);
    std::unordered_map<int, double> calculateKdensityTable(const std::unordered_map<int, double>& temperatureTable);
    std::vector<int>  evit(const std::unordered_map<int, double>& k_importance, const std::unordered_map<int, double>& k_density);
    //resultTable calculateKValues(const std::unordered_map<int, double>& importance_table, const std::unordered_map<int, double>& densityTable);
    bool cache_full();
    // std::unordered_map<int, std::pair<double, double>> calculateDensityAndImportance(const std::unordered_map<int, TemperatureRecord>& temperatureTable, const std::vector<trace_line>& trace_records);
private:
    std::list<std::pair<int, int>> _items; // (target, cache_addr) 
    std::unordered_map<int, std::list<std::pair<int, int>>::iterator> _table;

    std::unordered_map<int, double> scoreTable;


    int _c;
    unsigned int _hit_count;
    unsigned int _get_count;
    std::string _file_name;
};