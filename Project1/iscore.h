#pragma once

#include <unordered_map>
#include <list>
#include <sstream>
#include <string>
#include <cstdint>
#include <cmath>

// ========== 计时全局变量（extern 声明） ==========
extern double g_t_evict_ns;
extern double g_t_evict_traverse_ns;
extern uint64_t g_evict_count;

// ========================================================================
// ARC (Adaptive Replacement Cache) Configuration
// ========================================================================
namespace ARCConfig {
    // 统计打印间隔（每 N 次访问打印一次 p）
    constexpr uint64_t P_PRINT_INTERVAL = 100000;
}

/**
 * @struct ISCOREParams
 * @brief 缓存 get 操作的参数结构体（ARC 只需要 target）
 */
struct ISCOREParams {
    int target;            ///< 要访问的目标对象ID（block id）
    int size;              ///< 对象大小（ARC 中暂不使用，保持接口兼容）
};

/**
 * @class ISCORECache
 * @brief 标准 ARC (Adaptive Replacement Cache) 实现
 *
 * 核心结构：
 * - T1: recent list（最近访问的页面）
 * - T2: frequent list（频繁访问的页面）
 * - B1: ghost list for T1（从 T1 淘汰的页面）
 * - B2: ghost list for T2（从 T2 淘汰的页面）
 * - p: 自适应参数（0..C），控制 T1/T2 边界
 */
class ISCORECache {

public:
    explicit ISCORECache(int c, std::string file_name,
                         double lambda = 0.001,
                         double c_smooth = 1.0,
                         int evict_k = 1);

    ISCORECache(const ISCORECache&) = delete;
    ISCORECache& operator=(const ISCORECache&) = delete;
    ~ISCORECache() = default;

public:
    int get(const ISCOREParams& params);
    std::string statics();

private:
    // ========== ARC 四个 LRU 队列 ==========
    std::list<int> _t1;   ///< T1: recent list
    std::list<int> _t2;   ///< T2: frequent list
    std::list<int> _b1;   ///< B1: ghost for T1
    std::list<int> _b2;   ///< B2: ghost for T2

    // ========== 队列位置索引 ==========
    std::unordered_map<int, std::list<int>::iterator> _t1_pos;
    std::unordered_map<int, std::list<int>::iterator> _t2_pos;
    std::unordered_map<int, std::list<int>::iterator> _b1_pos;
    std::unordered_map<int, std::list<int>::iterator> _b2_pos;

    int _c;               ///< 总容量
    int _p;               ///< 自适应参数（0..C），初始 = C/2

    unsigned int _hit_count;
    unsigned int _get_count;
    std::string _file_name;

    // ========== 统计计数器 ==========
    uint64_t _t1_hit = 0;    ///< T1 命中次数
    uint64_t _t2_hit = 0;    ///< T2 命中次数
    uint64_t _b1_hit = 0;    ///< B1 ghost 命中次数
    uint64_t _b2_hit = 0;    ///< B2 ghost 命中次数
    uint64_t _evict_from_t1 = 0;  ///< 从 T1 淘汰次数
    uint64_t _evict_from_t2 = 0;  ///< 从 T2 淘汰次数

    // ========== 其他参数（保留以兼容接口） ==========
    double _lambda;
    double _c_smooth;
    int _evict_k;

private:
    void printConfig();

    /// 从队列中删除对象（辅助函数）
    void removeFromQueue(int obj_id, std::list<int>& queue, 
                         std::unordered_map<int, std::list<int>::iterator>& pos_map);

    /// 添加到队列 MRU（头部）
    void addToMRU(int obj_id, std::list<int>& queue,
                  std::unordered_map<int, std::list<int>::iterator>& pos_map);

    /// Replace 函数：根据 p 决定从 T1 还是 T2 淘汰
    void replace();

    /// 检查容量约束（断言）
    void checkConstraints() const;
};
