/**
 * @file score.h
 * @brief SCORE缓存替换算法头文件
 * 
 * 本文件定义了基于SCORE（温度-密度-重要性）的缓存替换算法。
 * SCORE算法结合三个指标来做出淘汰决策：
 * - 温度（Temperature）：使用指数衰减衡量访问的最近性
 * - 密度（Density）：温度按对象大小和年龄归一化
 * - 重要性（Importance）：访问频率计数
 */

#include <unordered_map>
#include <list>
#include <sstream>
#include <cstdint>
#include "TraceLine.h"

/**
 * @struct TemperatureRecord
 * @brief 表示缓存对象的温度记录结构体
 * 
 * 存储每个缓存对象的温度相关信息，用于跟踪访问模式并做出淘汰决策。
 */
struct TemperatureRecord {
    int object_id;                  ///< 缓存对象的唯一标识符
    double temperature;             ///< 当前温度值（值越高表示最近访问越频繁）
    double related_temperature;     ///< 考虑前一个访问模式的关联温度
    double last_access_time;        ///< 该对象最后访问的时间戳（单位：毫秒）
};

/**
 * @struct Meta
 * @brief 缓存对象的元数据结构体
 * 
 * 存储每个缓存对象的在线维护信息，用于淘汰决策。
 */
struct Meta {
    double temperature;      ///< 当前温度值（指数衰减更新）
    uint32_t last_req;       ///< 上次访问时间（逻辑时间戳：request_number）
    uint32_t freq;           ///< 访问频次（用于计算重要性）
    int size;                ///< 对象大小（字节）
};

/**
 * @struct SCOREParams
 * @brief SCORE缓存get操作的参数结构体
 * 
 * 包含SCORE计算所需的目标对象ID和当前请求号。
 */
struct SCOREParams {
    int target;              ///< 要访问的目标对象ID（block id）
    uint32_t now_req;        ///< 当前请求号（逻辑时间戳）
    int size_of_blocks;      ///< 对象包含的块数（用于计算大小）
};

/**
 * @class SCORECache
 * @brief SCORE缓存替换算法实现类
 * 
 * 实现基于SCORE算法的缓存替换策略，该算法结合了：
 * - 使用指数衰减的温度计算
 * - 密度计算（温度 / (大小 * 年龄)）
 * - 重要性计算（访问计数）
 * 
 * 淘汰决策：得分（k_importance + k_density）低于平均值的对象将被淘汰。
 */
class SCORECache {

public:
    /**
     * @brief SCORECache构造函数
     * @param c 缓存容量（最大对象数量）
     * @param file_name 正在处理的跟踪文件名
     */
    explicit SCORECache(int c, std::string file_name) :
        _c(c), _file_name(file_name), _hit_count(0), _get_count(0) {}
    
    // 禁用拷贝构造函数和赋值运算符
    SCORECache(const SCORECache&) = delete;
    SCORECache& operator=(const SCORECache&) = delete;

    /**
     * @brief 析构函数
     */
    ~SCORECache() {}

public:
    /**
     * @brief 主要的缓存访问函数
     * @param scoreparam 包含目标对象、当前请求号和对象大小的参数
     * @return 对象的缓存地址，错误时返回-1
     * 
     * 处理缓存命中（移动到前端、更新元数据）和未命中（如果缓存已满则淘汰，然后添加新对象）。
     * 命中时 O(1) 更新温度；未命中且缓存满时 O(cache_size) 扫描选出最低分对象淘汰。
     */
    int get(const SCOREParams& scoreparam);
    
    /**
     * @brief 获取格式化的缓存统计信息字符串
     * @return 包含缓存统计信息的字符串（命中率、请求计数等）
     */
    std::string statics();

private:
      //cache缓存层里面,存储的cache对象,是采取索引+ 存储的双层设计  是存储到_items列表中的,这个列表我们使用_table的哈希表来进行快速的查询和操作
             // 索引是采_table的哈希表来进行快速的查找存储cache对象的位置
             //存储是采用_items列表来进行存储的,存储cache对象的id 和cache层的物理地址

    std::list<std::pair<int, int>> _items;  //LRU风格的链表，存储(target_object_id, cache_address)对


    // 这里的_table.用于存放cache层的缓存数据
    // 它是一个map结构.键是块号  值是一个迭代器 ,迭代器可以快速的定位_items链表的数据
    std::unordered_map<int, std::list<std::pair<int, int>>::iterator> _table;  ///< 用于O(1)查找链表迭代器的哈希表

    std::unordered_map<int, Meta> _meta;  ///< 缓存对象的元数据表，key=block id，在线维护温度/频次/大小
    
    int _c;                    ///< 缓存容量（最大对象数量）
    unsigned int _hit_count;   ///< 缓存命中次数
    unsigned int _get_count;   ///< 缓存请求总次数
    std::string _file_name;    ///< 正在处理的跟踪文件名
};
