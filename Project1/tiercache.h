#pragma once
#include <chrono> // 确保包含此头文件
#include <iostream>
#include <vector>
#include <set>
#include <map>
#include "bloomfilter.h"
#include "histogram.h"

using namespace std;

class object_c {
public:
    std::chrono::system_clock::time_point mtime; // 使用 std::chrono 来记录时间
    int oid;
    int size; 
    object_c(int oid, int size) {
        mtime = chrono::system_clock::now(); // 获取当前时间
        this->oid = oid;
        this->size = size;
    }
    // 构造函数使用当前时间初始化 mtime
    /*object_c(int oid, int size) : oid(oid), size(size) {
        mtime = std::chrono::system_clock::now();
    }*/
    bool operator<(const object_c& p) const {
        return this->oid > p.oid;
    }
};


class CephTierCache {
private:
    list<object_c> obj_set;
    map<int, list<object_c>::iterator> obj_map;
    vector<uint32_t> grade_table;
    pow2_hist_t temp_hist;
    BloomFilter* hit_set;
    double _capacity;
    double _current_size;
    double _hit_count;
    double _get_count;
    std::string _file_name;

    double cache_min_evict_age = 0.8;
    uint32_t hit_set_count = 3;
    uint32_t hit_set_grade_decay_rate = 40;
    uint32_t bloomfilter_max = 10000;
    uint32_t hit_set_search_last_n = 3;
    map<time_t, BloomFilter> hit_set_map;
    unsigned evict_effort = 5000;
    double osd_pool_default_cache_max_evict_check_size = 0.005;
    list<object_c>::iterator _next;

public:
    uint32_t get_grade(unsigned i) const;
    void calc_grade_table();
    bool get(int oid, int size);

    void agent_estimate_temp(const list<object_c>::iterator& it, int* temp);
    bool agent_maybe_evict(list<object_c>::iterator& it);
    int objects_list_partial(vector<list<object_c>::iterator>& ls);
    bool agent_work();
    void renew_hit_set();
    void statics();

    explicit CephTierCache(int size, string fliename) {
        this->_capacity = size;
        this->_file_name = fliename;
        this->hit_set = new BloomFilter(bloomfilter_max);
        this->_current_size = 0;
        this->_hit_count = 0;
        this->_get_count = 0;
        calc_grade_table();
        obj_set.clear();
        obj_map.clear();
        _next = obj_set.begin();
    }
};
