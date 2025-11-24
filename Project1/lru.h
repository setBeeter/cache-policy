#pragma once

#include <unordered_map>
#include <list>
#include <sstream>
#include <cstdint>


class LRUCache {

public:
    explicit LRUCache(int c, std::string file_name) :
        _capacity(c), _file_name(file_name), _hit_count(0), _get_count(0) {}

    LRUCache(const LRUCache&) = delete;
    LRUCache& operator=(const  LRUCache&) = delete;

    ~LRUCache() {}

public:
    int get(int target);
    std::string statics();

private:
    std::list<std::pair<int, int>> _items; // (target, cache_addr) 链表
    std::unordered_map<int, std::list<std::pair<int, int>>::iterator> _table;//哈希表
    int _capacity;
    int _miss_count;  // 新增加的未命中计数器
    unsigned int _hit_count;
    unsigned int _get_count;
    std::string _file_name;
};
