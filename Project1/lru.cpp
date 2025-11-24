#include "lru.h"
int LRUCache::get(int target) {
    if (_capacity <= 0) {
        return -1;
    }

    ++_get_count;
    auto it = _table.find(target);
    if (it != _table.end()) {

        ++_hit_count;
        _items.splice(_items.begin(), _items, it->second);
        return _items.front().second;
    }
    else {
        // 缓存未命中
        _miss_count++;  // 增加未命中计数器
        if (_items.size() >= _capacity) {
            _table.erase(_items.back().first);
            _items.pop_back();
        }
        _items.emplace_front(target, target);
        _table[target] = _items.begin();
        return _items.front().second;
    }
    /*当缓存未命中时，需要淘汰一个对象以腾出空间。

    首先，将要淘汰的对象放在队尾（因为它是最久未被访问的对象，符合LRU策略）。

    然后，将新访问的对象插入到队列的前面，表示它是最近访问的对象。

    最后，淘汰队尾的对象，这样就完成了淘汰操作。*/
}
std::string LRUCache::statics() {
    std::stringstream s;
    s << "trace:" << _file_name << " lru_cache:"
        << " cache_size:" << _capacity
        << " request:" << _get_count
        << " hit:" << _hit_count
        << " miss:" << _miss_count  // 添加未命中次数
        << " hit_rate:" << 1.0 * _hit_count / _get_count << std::endl;
    return s.str();
}