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
 * @struct SCOREParams
 * @brief SCORE缓存get操作的参数结构体
 * 
 * 包含SCORE计算所需的目标对象ID和跟踪记录。
 */
struct SCOREParams {
    int target;                                     ///< 要访问的目标对象ID
    const std::vector<trace_line>& trace_records;  ///< 用于计算的跟踪记录引用
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
     * @param scoreparam 包含目标对象和跟踪记录的参数
     * @return 对象的缓存地址，错误时返回-1
     * 
     * 处理缓存命中（移动到前端）和未命中（如果缓存已满则淘汰，然后添加新对象）。
     * 当缓存已满时，执行完整的SCORE计算以确定淘汰候选对象。
     */
    int get(const SCOREParams& scoreparam);
    
    /**
     * @brief 获取格式化的缓存统计信息字符串
     * @return 包含缓存统计信息的字符串（命中率、请求计数等）
     */
    std::string statics();
    
    /**
     * @brief 计算跟踪记录中所有对象的温度
     * @param trace_records 跟踪行记录向量
     * @return 从对象ID到TemperatureRecord的映射
     * 
     * 使用指数衰减公式：T_new = T_old * exp(-k * age)，其中k=0.5
     * 新对象的初始温度为1000.0
     */
    std::unordered_map<int, TemperatureRecord> calculateTemperature(const std::vector<trace_line>& trace_records);
    
    /**
     * @brief 计算所有对象的温度密度
     * @param temperatureTable 对象温度映射表
     * @param trace_records 跟踪行记录向量
     * @return 从对象ID到密度值的映射
     * 
     * 密度公式：density = 10 * temperature / (object_size * age)
     * 密度越高表示对象越有价值（热数据、小对象、最近访问）
     */
    std::unordered_map<int, double> calculateDensity(const std::unordered_map<int, TemperatureRecord>& temperatureTable, const std::vector<trace_line>& trace_records);
    
    /**
     * @brief 计算所有对象的重要性（访问计数）
     * @param trace_records 跟踪行记录向量
     * @return 从对象ID到重要性值（access_count）的映射
     * 
     * 重要性基于跟踪记录中的access_count字段。
     * 仅记录每个对象ID的第一次出现。
     */
    std::unordered_map<int, double> calculateImportance(const std::vector<trace_line>& trace_records);
    
    /**
     * @brief 归一化重要性表并计算k_importance
     * @param importance_table 对象重要性值映射表
     * @return 从对象ID到归一化k_importance值的映射
     * 
     * 归一化公式：normalized = (value - min) / (max - min)
     * k_importance = importance * normalized_value
     */
    std::unordered_map<int, double> normalizeImportanceTable(const std::unordered_map<int, double>& importance_table);
    
    /**
     * @brief 归一化密度表并计算k_density
     * @param densityTable 对象密度值映射表
     * @return 从对象ID到归一化k_density值的映射
     * 
     * 归一化公式：normalized = (value - min) / (max - min)
     * k_density = density * normalized_value
     */
    std::unordered_map<int, double> calculateKdensityTable(const std::unordered_map<int, double>& densityTable);
    
    /**
     * @brief 基于SCORE确定要淘汰的对象
     * @param k_importance 归一化重要性值映射表
     * @param k_density 归一化密度值映射表
     * @return 要淘汰的对象ID向量
     * 
     * 为每个对象计算得分：score = k_importance + k_density
     * 淘汰得分低于平均得分的对象。
     */
    std::vector<int> evit(const std::unordered_map<int, double>& k_importance, const std::unordered_map<int, double>& k_density);
    
    /**
     * @brief 检查缓存是否已满
     * @return 如果缓存大小 >= 容量则返回true，否则返回false
     */
    bool cache_full();

private:
    std::list<std::pair<int, int>> _items;  ///< LRU风格的链表，存储(target_object_id, cache_address)对
    std::unordered_map<int, std::list<std::pair<int, int>>::iterator> _table;  ///< 用于O(1)查找链表迭代器的哈希表
    
    std::unordered_map<int, double> scoreTable;  ///< 缓存对象的得分表（当前实现中未使用）
    
    int _c;                    ///< 缓存容量（最大对象数量）
    unsigned int _hit_count;   ///< 缓存命中次数
    unsigned int _get_count;   ///< 缓存请求总次数
    std::string _file_name;    ///< 正在处理的跟踪文件名
};
