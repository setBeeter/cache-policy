#include "tdc2.h"
#include <cassert> 
#include <iostream>
#include <chrono>
#include <iomanip>
using namespace std;
using namespace std::chrono;
//初始化一个称为 grade_table 的数组，该数组用于存储不同命中次数的分值。分值随着命中次数的增加而递减，递减率由 hit_set_grade_decay_rate 控制。
void tdcCache::calc_grade_table() {
    unsigned v = 1000000;
    grade_table.resize(hit_set_count);
    for (unsigned i = 0; i < hit_set_count; i++) {
        v = v * (1 - (hit_set_grade_decay_rate / 100.0));
        grade_table[i] = v;
    }
}
//这个方法根据命中次数返回对应的分数。如果提供的索引超出了 grade_table 的大小，它将返回 0。
uint32_t tdcCache::get_grade(unsigned i) const {
    if (grade_table.size() <= i)
        return 0;
    return grade_table[i];
}

bool tdcCache::get(int oid, int size) {
    ++_get_count;//请求计数器
    object_c obj(oid, size);
    //检查缓存命中
    if (obj_map.find(oid) != obj_map.end()) {
        ++_hit_count;
        hit_set->setBit(obj.oid);
        return true;
    }
    //更新当前大小
    _current_size += obj.size;
    if (_current_size > _capacity) {
        //可能进行缓存清理或逐出操作
        agent_work();
    }
    obj_set.push_front(obj);
    obj_map[oid] = obj_set.begin();// 在obj_map中更新这个对象的位置。
    // 在hit_set中设置该对象的位，表示它被访问
    hit_set->setBit(obj.oid);
    //如果hit_set的剩余容量不足，调用renew_hit_set()方法来更新或刷新hit_set。
    if (hit_set->remain_capacity() <= 0) {
        renew_hit_set();
    }
    //这是一个断言，用于确保obj_map和obj_set的大小一致，保持数据结构的一致性。
    assert(obj_map.size() == obj_set.size());
    return true;
}
//它调用 objects_list_partial 来获取一部分对象，然后调用 agent_maybe_evict 来决定是否淘汰这些对象。
// 这个方法执行代理的工作，选择一部分对象进行淘汰检查，返回是否成功执行工作。

bool tdcCache::agent_work() {
    vector<list<object_c>::iterator> ls;//创建了一个类型为ls
    int selectedCount = objects_list_partial(ls);
    if (selectedCount > 0) {
       
        // 打印 ls 中的对象信息
        std::cout << "Objects in ls after maybe_evict:" << std::endl;
        for (auto it = ls.begin(); it != ls.end(); ++it) {
            // 解引用迭代器以访问 object_c 对象，并打印信息
            object_c& obj = **it; // 注意双重解引用，因为 ls 存储的是指向 list 中对象的迭代器
            std::cout << "Object ID: " << obj.oid << ", Size: " << obj.size << std::endl;
        }
        return agent_maybe_evict(ls); // 返回代理是否成功执行工作
    }
    return false; // 如果没有选择任何对象，则返回false表示工作未成功执行
}
//这个方法决定是否需要从缓存中淘汰一些对象。它根据对象的“温度”和大小来计算密度，然后与平均密度进行比较，低于平均密度的对象可能被淘汰。
bool tdcCache::agent_maybe_evict(vector<list<object_c>::iterator>& ls) {
    vector<pair<int, list<object_c>::iterator>> v;
    uint64_t dentisy_sum = 0;

    for (int i = 0; i < ls.size(); ++i) {
        auto now = system_clock::now();
        auto obj_local_mtime = ls[i]->mtime;

        std::time_t mtime_t = std::chrono::system_clock::to_time_t(obj_local_mtime);
        char buffer[26]; // ctime_s 需要一个足够大的缓冲区来存储结果字符串
        errno_t err = ctime_s(buffer, sizeof(buffer), &mtime_t);
        if (err == 0) {
            std::cout << "mtime: " << buffer;
        }
        else {
            std::cerr << "Error formatting time\n";
        }
        // 计算时间差并判断是否过早逐出
        if (obj_local_mtime + seconds(static_cast<long long>(cache_min_evict_age)) > now) {
            // 如果对象太“年轻”，则跳过
            continue;
        }

        int temp = 0;
        if (hit_set)
            agent_estimate_temp(ls[i], &temp);

        // 计算时间差并转换为秒
        auto duration = duration_cast<seconds>(now - obj_local_mtime).count();
        std::cout << "Duration: " << duration << " seconds" << std::endl;

        int density = 0;
        if (ls[i]->size * duration != 0) { // 避免除零错误
            density = temp / (ls[i]->size * duration);
        }
        else {
            std::cout << "Warning: division by zero (size * duration is 0)." << std::endl;
            continue; // 可以选择跳过这次循环
        }

        dentisy_sum += density;

        v.push_back({ density, ls[i] });
    }
    std::cout << "Size of v: " << v.size() << std::endl; // 打印 v 的大小
    uint64_t density_avg = dentisy_sum / v.size();

    for (int i = 0; i < v.size(); ++i) {
        if (v[i].first > density_avg) {
            continue;
        }
        auto it = v[i].second;
        if (obj_map.find(it->oid) == obj_map.end()) {
            return false;
        }
        _current_size -= it->size;
        auto temp_it = obj_map[it->oid];
        obj_map.erase(it->oid);
        _next = obj_set.erase(temp_it);
    }
}

//这个方法从缓存中选择一部分对象进行淘汰检查。
int tdcCache::objects_list_partial(vector<list<object_c>::iterator>& ls) {
    // 如果容器为空，则直接返回
    if (obj_set.empty()) {
        ls.clear(); // 确保输出参数反映容器的当前状态
        return 0; // 没有选中的对象
    }

    int n = max(1, static_cast<int>(obj_map.size() * osd_pool_default_cache_max_evict_check_size));
    ls.resize(n);
    int selectedCount = 0; // 用于记录选择的对象数量

    for (int i = 0; i < n; ++i) {
        if (_next == obj_set.end()) {
            _next = obj_set.begin();
        }

        // 这里不需要再检查 _next != obj_set.end()，因为上面已经处理了容器为空的情况
        ls[i] = _next;
        ++_next;
        ++selectedCount;
    }

    return selectedCount;
}



//这个方法估计给定对象在缓存中的“温度”（一种评估对象重要性的指标）。它根据对象在不同时间点的命中情况来计算温度。
void tdcCache::agent_estimate_temp(const list<object_c>::iterator& it, int* temp) {
    *temp = 0;
    if (hit_set->checkBit(it->oid))
        *temp = 1000000;
    unsigned i = 0;
    int last_n = hit_set_search_last_n;
    for (map<time_t, BloomFilter>::reverse_iterator p = hit_set_map.rbegin(); last_n > 0 && p != hit_set_map.rend(); ++p, ++i) {
        if (p->second.checkBit(it->oid)) {
            *temp += get_grade(i);
            --last_n;
        }
    }
}

// class density_compare{ 
// public:
//     bool operator()(pair<int,list<object_c>::iterator> a, pair<int,list<object_c>::iterator> b){
//         return a.first < b.first;
//     }
// };


//当需要的时候，这个方法会更新 hit_set（可能是一个布隆过滤器，用于快速检查对象是否存在于缓存中）。
void tdcCache::renew_hit_set() {
    time_t t;
    time(&t);
    t += hit_set_map.size();
    hit_set_map.insert(pair<time_t, BloomFilter>(t, *(hit_set)));
    BloomFilter* bf = new BloomFilter(bloomfilter_max);
    hit_set = bf;
}

void tdcCache::statics() {
    cout << "trace:" << _file_name << " tdc_cache:"
        << "\tcache_size:" << _capacity
        << "\trequest:" << _get_count
        << "\thit:" << _hit_count
        << "\thit_rate: " << 1.0 * _hit_count / _get_count << endl;
}
