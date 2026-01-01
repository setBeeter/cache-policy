#ifndef TDC_H
#define TDC_H

#include <unordered_map>
#include <list>
#include <sstream>
#include <cstdint>
#include <chrono>
struct temp {
    int n;  // 唯一标识
    double temperature; // 温度
    int size; // 大小
    std::chrono::system_clock::time_point last_access_time; // 最后访问时间
};
//TDCParams 结构体：唯一标识一个映射，映射将在内部映射到相应的 temp 实例
struct TDCParams {
    int target;  // 唯一标识
    int n; // 周期
    double size;
    std::unordered_map<int, std::unordered_map<int, temp>> temperatureTable; //温度表
};
//temp 结构体用于存储温度、大小和时间。


class TDCCache {

public:
    explicit TDCCache(int c, std::string file_name) :
        _capacity(c), _file_name(file_name), _hit_count(0), _get_count(0) {}

    TDCCache(const TDCCache&) = delete;
    TDCCache& operator=(const TDCCache&) = delete;

    ~TDCCache() {}

public:
    int get(const TDCParams& params);
    std::string statics();
    //double calculateTemperature(int target);
    int currentCycleAccessCount;  //当前周期的访问
   //使用一个哈希 std::unordered_map<int, TDCParams>来存储每个 TDCParams 实例
    std::unordered_map<int, std::unordered_map<int,temp>> temperatureTable;
    // 存储温度以及节点的缓存哈希
    //std::unordered_map<int, double> densityTable;
private:
    //一个存储数据的 _items
    std::list<std::pair<int, int>> _items; // (target, cache_addr)
    std::unordered_map<int, std::list<std::pair<int, int>>::iterator> _table;//哈希存储每个位置的映射 _table
    int _capacity;
    unsigned int _hit_count;
    unsigned int _get_count;
    std::string _file_name;
};

#endif  // TDC_H
