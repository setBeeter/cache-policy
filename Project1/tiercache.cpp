#include "tiercache.h"
#include <chrono>
#include <list>
#include <map>
#include <iostream>
#include <cassert> 
using namespace std;
using namespace std::chrono;
//grade_table的数组,数组中的每个元素代表不同命中次数下的分值，初始值为1000000，后续每次减少hit_set_grade_decay_rate百分比。
void CephTierCache::calc_grade_table() {
    unsigned v = 1000000;
    grade_table.resize(hit_set_count);
    for (unsigned i = 0; i < hit_set_count; i++) {
        v = v * (1 - (hit_set_grade_decay_rate / 100.0));
        grade_table[i] = v;

    }
}
//根据命中次数i获取对应的分值。如果i超过了grade_table的大小，返回0
uint32_t CephTierCache::get_grade(unsigned i) const {
    if (grade_table.size() <= i)
        return 0;
    return grade_table[i];
}
//管理缓存的获取操作,增加_get_count，判断对象是否已在缓存中（即命中），如果未命中，添加新对象并可能触发agent_work()以维持缓存大小。
bool CephTierCache::get(int oid, int size) {
    ++_get_count;
    object_c obj(oid, size);
    if (obj_map.find(oid) != obj_map.end()) {
        ++_hit_count;
        hit_set->setBit(obj.oid);
        return true;
    }
    _current_size += obj.size;
    if (_current_size > _capacity) {
        agent_work();
    }
    obj_set.push_front(obj);
    obj_map[oid] = obj_set.begin();
    hit_set->setBit(obj.oid);

    if (hit_set->remain_capacity() <= 0) {
        renew_hit_set();
    }
    assert(obj_map.size() == obj_set.size());
    return true;
}

//评估给定对象的“温度”（可能是一种优先级或重要性的度量），基于其历史命中情况。
void CephTierCache::agent_estimate_temp(const list<object_c>::iterator& it, int* temp) {
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

//生成一个部分对象列表，这些对象可能会被逐出。
int CephTierCache::objects_list_partial(vector<list<object_c>::iterator>& ls) {
    int n = obj_map.size() * osd_pool_default_cache_max_evict_check_size;//计算要选择的对象数量
    n = max(1, n);//保证函数至少处理一个对象。
    ls.resize(n);//调整输出向量的大小
    auto it = _next; // 使用局部迭代器来避免修改 _next 的状态
    for (int i = 0; i < n; ++i) {
        // 确保迭代器没有到达 obj_set 的末尾
        if (it == obj_set.end()) {
            it = obj_set.begin(); // 如果到达末尾，重新开始
        }

        // 检查当前对象是否在 obj_map 中
        if (obj_map.find(it->oid) == obj_map.end()) {
            // 如果对象不在 obj_map 中，这通常表示逻辑错误
            // 你可以在这里添加错误处理逻辑
            break;
        }

        ls.push_back(it); // 添加迭代器到 ls

        // 移动到下一个元素，确保不会越界
        ++it;
        if (it == obj_set.end() && i < n - 1) {
            it = obj_set.begin(); // 防止最后一个元素后还有更多迭代
        }
    }
    // 更新 _next 为下一次迭代的开始点
    _next = it;
    return n; // 返回所选对象的数量
}

//根据从objects_list_partial()得到的候选对象列表,执行实际的逐出操作
bool CephTierCache::agent_work() {
    vector<list<object_c>::iterator> ls;
    int selectedCount = objects_list_partial(ls);
    bool workExecuted = false;

    // 在尝试逐出前打印 ls 中的对象信息
    std::cout << "Selected objects for possible eviction:" << std::endl;
    for (int i = 0; i < ls.size(); ++i) {
        // 解引用迭代器访问 object_c 对象，并打印 oid 和 size
        object_c& obj = *(ls[i]); // 注意双重解引用
        std::cout << "Object ID: " << obj.oid << ", Size: " << obj.size << std::endl;

        if (agent_maybe_evict(ls[i])) {
            workExecuted = true;
        }
    }

    return workExecuted; // 返回是否成功执行工作
}
//判断是否应该从缓存中逐出（移除）特定对象。这涉及检查对象的“温度”，以及它是否太新或太“热”（即经常被访问）。
bool CephTierCache::agent_maybe_evict(list<object_c>::iterator& it) {
    // 使用 std::chrono 获取当前时间点
    auto now = system_clock::now();
    // 假设 object_c 类中的 mtime 已经被修改为 std::chrono::system_clock::time_point 类型
    auto obj_local_mtime = it->mtime;
    // 计算时间差，检查对象是否"太年轻"被逐出
    // cache_min_evict_age 需要是以秒为单位的持续时间
    if (obj_local_mtime + seconds(static_cast<long long>(cache_min_evict_age)) > now) {
        // 如果对象的修改时间加上最小逐出年龄大于当前时间，则跳过
        return false;
    }
    int temp = 0;
    uint64_t temp_upper = 0, temp_lower = 0;
    if (hit_set)
        agent_estimate_temp(it, &temp);
    temp_hist.add(temp);

    temp_hist.get_position_micro(temp, &temp_lower, &temp_upper);
    if (1000000 - temp_upper <= evict_effort) {
        // cout<<__func__<<" skip (too hot) "<<it->oid<<" "<<temp<<" upper: "<<temp_upper<<endl;
        return false;
    }
    // cout<<__func__<<"\t"<<it->oid<<"\t"<<temp<<"\t"<<temp_upper<<endl;
    if (obj_map.find(it->oid) == obj_map.end()) {
        return false;
    }
    _current_size -= it->size;
    auto temp_it = obj_map[it->oid];
    obj_map.erase(it->oid);    //map<int, list<object_c>::iterator> obj_map;
    _next = obj_set.erase(temp_it);   //list<object_c> obj_set;
    return true;
}
//当命中集合的容量不足时，创建新的命中集合，并保留旧的命中数据。
void CephTierCache::renew_hit_set() {
    time_t t;
    time(&t);
    t += hit_set_map.size();
    hit_set_map.insert(pair<time_t, BloomFilter>(t, *(hit_set)));
    BloomFilter* bf = new BloomFilter(bloomfilter_max);
    hit_set = bf;
}

void CephTierCache::statics() {
    cout << "trace:" << _file_name << " ceph_tier_cache:"
        << "\tcache_size:" << _capacity
        << "\trequest:" << _get_count
        << "\thit:" << _hit_count
        << "\thit_rate: " << 1.0 * _hit_count / _get_count << endl;
}
