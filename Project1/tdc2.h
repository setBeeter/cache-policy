
#include "tiercache.h"
#include <unordered_map>
#include <list>
#include <sstream>
#include <cstdint>
#include <chrono>
using namespace std;

class tdcCache {
private:
    list<object_c> obj_set;//╤сап
    map<int, list<object_c>::iterator> obj_map;
    vector<uint32_t> grade_table;
    pow2_hist_t temp_hist;
    BloomFilter* hit_set;
    int _capacity;
    int _current_size;
    int _hit_count;
    int _get_count;
    std::string _file_name;

    double cache_min_evict_age = 0.8;
    uint32_t hit_set_count = 3;
    uint32_t hit_set_grade_decay_rate = 20;
    uint32_t bloomfilter_max = 100000;
    uint32_t hit_set_search_last_n = 3;
    map<time_t, BloomFilter> hit_set_map;
    unsigned evict_effort = 0;
    double osd_pool_default_cache_max_evict_check_size = 0.00001;
    list<object_c>::iterator _next;

public:
    uint32_t get_grade(unsigned i) const;
    void calc_grade_table();
    bool get(int oid, int size);

    void agent_estimate_temp(const list<object_c>::iterator& it, int* temp);
    bool agent_maybe_evict(vector<list<object_c>::iterator>& ls);
    int objects_list_partial(vector<list<object_c>::iterator>& ls);
    bool agent_work();
    void renew_hit_set();
    void statics();

    explicit tdcCache(int size, string fliename) {
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
