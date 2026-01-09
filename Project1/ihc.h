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
// IHC (Interval-aware Hot/Warm Cache) Configuration
// ========================================================================
namespace IHCConfig {
    // 统计打印间隔（每 N 次访问打印一次 p_hot）
    constexpr uint64_t P_PRINT_INTERVAL = 100000;
    
    // 默认参数
    constexpr int DEFAULT_INTERVAL_LEN = 10000;      // 每个 interval 的请求数
    constexpr double DEFAULT_ALPHA = 0.9;            // EMA 衰减系数（越大衰减越慢）
    constexpr double DEFAULT_TH_UP = 0.5;             // WARM -> HOT 晋升阈值
    constexpr double DEFAULT_TH_DOWN = 0.3;           // HOT -> WARM 降级阈值（必须 < Th_up）
    constexpr int DEFAULT_STEP_RULE = 1;              // 自适应步长规则：0=固定1, 1=动态比例
}

/**
 * @struct IHCParams
 * @brief 缓存 get 操作的参数结构体
 */
struct IHCParams {
    int target;            ///< 要访问的目标对象ID（block id）
    int size;              ///< 对象大小（暂不使用，保持接口兼容）
};

/**
 * @struct HeatMeta
 * @brief 对象的热度元数据
 */
struct HeatMeta {
    double heat;                    ///< EMA 热度值
    int last_interval_id;           ///< 上次访问时的 interval_id（用于懒更新）
    
    HeatMeta() : heat(0.0), last_interval_id(-1) {}
    HeatMeta(double h, int lid) : heat(h), last_interval_id(lid) {}
};

/**
 * @class IHCCache
 * @brief IHC (Interval-aware Hot/Warm Cache) 实现
 *
 * ========== 核心设计理念 ==========
 *
 * IHC 的核心机制：
 * - 双温区真实缓存：HOT + WARM（都存真实对象）
 * - 两个 ghost 队列：G_hot + G_warm（只存 key，不存 value）
 * - 分区依据：区间级热度 heat(x) + 迟滞门控（Th_up > Th_down）
 *   * 对象晋升/降级由 heat(x) 的门控触发
 *   * heat(x) 通过 EMA 在 interval 粒度上计算，支持懒更新
 * - 自适应参数 p_hot：控制 HOT 目标容量，通过 G_hot/G_warm ghost hit 调整
 *
 * ========== 算法结构 ==========
 * - HOT: 热区真实缓存（LRU 队列）
 * - WARM: 暖区真实缓存（LRU 队列）
 * - G_hot: 从 HOT 淘汰的 ghost 队列（只存 key）
 * - G_warm: 从 WARM 淘汰的 ghost 队列（只存 key）
 * - p_hot: 自适应参数（0..C），表示 HOT 目标容量
 *
 * ========== 容量约束 ==========
 * - |HOT| + |WARM| <= C（真实缓存总容量）
 * - |G_hot| + |G_warm| <= C（ghost 总容量上限）
 *
 * ========== 热度计算（区间级 EMA） ==========
 * - 访问序列按 request_count 切 interval：每 interval_len 次请求算一个 interval
 * - 每个对象维护：heat (EMA), last_interval_id
 * - 懒更新：touch(x) 时根据 (global_interval_id - last_interval_id[x]) 对 heat 做衰减
 *   * heat *= pow(alpha, gap)
 *   * heat = alpha*heat + (1-alpha)*1（增量更新）
 *   * 更新 last_interval_id[x] = global_interval_id
 *
 * ========== 迟滞门控 ==========
 * - Th_up > Th_down（必须满足）
 * - WARM -> HOT: heat(x) >= Th_up
 * - HOT -> WARM: heat(x) <= Th_down
 * - 防止区间边界热度波动导致频繁迁移
 */
class IHCCache {

public:
    explicit IHCCache(int c, std::string file_name,
                        int interval_len = IHCConfig::DEFAULT_INTERVAL_LEN,
                        double alpha = IHCConfig::DEFAULT_ALPHA,
                        double th_up = IHCConfig::DEFAULT_TH_UP,
                        double th_down = IHCConfig::DEFAULT_TH_DOWN,
                        int step_rule = IHCConfig::DEFAULT_STEP_RULE);

    IHCCache(const IHCCache&) = delete;
    IHCCache& operator=(const IHCCache&) = delete;
    ~IHCCache() = default;

public:
    int get(const IHCParams& params);
    std::string statics();

private:
    // ========== 双温区真实缓存 ==========
    std::list<int> _hot;      ///< HOT: 热区真实缓存（LRU）
    std::list<int> _warm;     ///< WARM: 暖区真实缓存（LRU）

    // ========== Ghost 队列（只存 key） ==========
    std::list<int> _g_hot;    ///< G_hot: 从 HOT 淘汰的 ghost
    std::list<int> _g_warm;   ///< G_warm: 从 WARM 淘汰的 ghost

    // ========== 队列位置索引 ==========
    std::unordered_map<int, std::list<int>::iterator> _hot_pos;
    std::unordered_map<int, std::list<int>::iterator> _warm_pos;
    std::unordered_map<int, std::list<int>::iterator> _g_hot_pos;
    std::unordered_map<int, std::list<int>::iterator> _g_warm_pos;

    // ========== 热度元数据 ==========
    std::unordered_map<int, HeatMeta> _heat_map;  ///< key -> (heat, last_interval_id)

    // ========== 全局计数器和 interval ==========
    uint64_t _global_req_id;          ///< 全局请求计数器
    int _global_interval_id;           ///< 当前 interval ID
    int _interval_len;                 ///< 每个 interval 的请求数

    // ========== 核心参数 ==========
    int _c;                             ///< 总容量
    int _p_hot;                         ///< HOT 目标容量（0..C），初始 = C/2
    double _alpha;                      ///< EMA 衰减系数
    double _th_up;                      ///< WARM -> HOT 晋升阈值
    double _th_down;                    ///< HOT -> WARM 降级阈值（必须 < Th_up）
    int _step_rule;                     ///< 自适应步长规则：0=固定1, 1=动态比例

    // ========== 统计计数器 ==========
    uint64_t _get_count = 0;
    uint64_t _real_hit = 0;             ///< 真实命中（HOT + WARM）
    uint64_t _miss = 0;                  ///< 真实 miss（cold miss + ghost hit）
    uint64_t _ghost_hit_hot = 0;        ///< G_hot ghost hit（算 miss）
    uint64_t _ghost_hit_warm = 0;       ///< G_warm ghost hit（算 miss）
    uint64_t _hot_hit = 0;               ///< HOT 命中次数
    uint64_t _warm_hit = 0;              ///< WARM 命中次数
    uint64_t _evict_from_hot = 0;        ///< 从 HOT 淘汰次数
    uint64_t _evict_from_warm = 0;       ///< 从 WARM 淘汰次数
    uint64_t _promote_warm_to_hot = 0;   ///< WARM -> HOT 晋升次数
    uint64_t _demote_hot_to_warm = 0;    ///< HOT -> WARM 降级次数

    std::string _file_name;

private:
    void printConfig();

    /// 从队列中删除对象（辅助函数）
    void removeFromQueue(int obj_id, std::list<int>& queue,
                         std::unordered_map<int, std::list<int>::iterator>& pos_map);

    /// 添加到队列 MRU（头部）
    void addToMRU(int obj_id, std::list<int>& queue,
                  std::unordered_map<int, std::list<int>::iterator>& pos_map);

    /// 更新对象热度（懒更新，基于 interval gap）
    void updateHeat(int obj_id);

    /// Replace 函数：根据 p_hot 决定从 HOT 还是 WARM 淘汰
    void replace();

    /// 检查容量约束（断言）
    void checkConstraints() const;
};

