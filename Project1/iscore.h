#pragma once

#include <unordered_map>
#include <list>
#include <vector>
#include <functional>
#include <sstream>
#include <string>
#include <cstdint>
#include <cmath>
#include "TraceLine.h"
#include "updatable_min_heap.h"

/**
 * @struct ISTemperatureRecord
 * @brief 目前保留自 SCORE 的温度记录结构体（如后续需要可扩展）
 */
struct ISTemperatureRecord {
    int object_id;
    double temperature;
    double related_temperature;
    double last_access_time;
};

/**
 * @struct ISMeta
 * @brief ISCORE 缓存对象的元数据结构体
 *
 * 语义与 SCORE 中 Meta 相同，只是命名区分以防后续扩展。
 */
struct ISMeta {
    double temperature;      ///< 当前温度值（指数衰减更新）
    uint32_t last_req;       ///< 上次访问时间（逻辑时间戳：request_number）
    uint32_t freq;           ///< 访问频次（用于计算重要性）
    int size;                ///< 对象大小（字节）
};

/**
 * @struct ISCOREParams
 * @brief ISCORE 缓存 get 操作的参数结构体
 *
 * 与 SCOREParams 结构保持相同字段与含义，便于在 main 中复用逻辑。
 */
struct ISCOREParams {
    int target;              ///< 要访问的目标对象ID（block id）
    uint32_t now_req;        ///< 当前请求号（逻辑时间戳）
    int size_of_blocks;      ///< 对象包含的块数（用于计算大小）
};

/**
 * @class ISCORECache
 * @brief 带 interval-hotness 权重的 SCORE 扩展版本
 *
 * 在原 SCORE 的 eviction score 基础上引入 region 级别的 interval-hotness 权重：
 * - region 以 block_id / REGION_SIZE_BLOCKS 划分（本工程中 target 即 block_id）
 * - 仅在淘汰排序阶段对 score 乘以 wInterval(rid)，不改变 admission / 预取 / 写回 / Ceph 语义
 */
class ISCORECache {

public:
    explicit ISCORECache(int c, std::string file_name) :
        _c(c),
        _hit_count(0),
        _get_count(0),
        _file_name(std::move(file_name)),
        _access_seq(0),
        // trace 中 block=512B；若按 4MB region，则 region 大小应为 4MB / 512B = 8192 blocks
        _region_size_blocks(8192),
        // 逻辑时间粒度为“每个 block 访问一次”，适度加快 region 热度衰减，避免历史热度长时间不消退
        _lambda(1.0 / 10000.0),
        _alpha(0.3),
        _smooth_c(5.0) {
        // 大 cache 下减少哈希表 rehash 以及堆扩容带来的抖动（不改变策略语义）
        if (_c > 0) {
            _table.reserve(static_cast<size_t>(_c) * 2 + 1);
            _meta.reserve(static_cast<size_t>(_c) * 2 + 1);
            _victim_heap.reserve(static_cast<size_t>(_c) + 1);

            // region 数量大致为 (key_space / region_size)，这里按 cache size 做一个保守上限
            _region_hot.reserve(static_cast<size_t>(_c) / 4 + 1);
            _region_last_seq.reserve(static_cast<size_t>(_c) / 4 + 1);
        }
    }

    ISCORECache(const ISCORECache&) = delete;
    ISCORECache& operator=(const ISCORECache&) = delete;

    ~ISCORECache() {}

public:
    /**
     * @brief 主要的缓存访问函数（接口与 SCORECache::get 保持一致语义）
     */
    int get(const ISCOREParams& scoreparam);

    /**
     * @brief 获取格式化的缓存统计信息字符串
     */
    std::string statics();

private:
    /**
     * @brief 计算给定对象的 ISCORE 分数（含 interval weight）
     */
    double calculateScoreWithInterval(int block_id, uint32_t now_req);

    // ========== 与 SCORE 相同的缓存结构 ==========
    std::list<std::pair<int, int>> _items;  // LRU 风格链表，存储 (target_object_id, cache_address)
    std::unordered_map<int, std::list<std::pair<int, int>>::iterator> _table;
    std::unordered_map<int, ISMeta> _meta;  ///< 缓存对象的元数据表，key = block id

    int _c;                    ///< 缓存容量（最大对象数量）
    unsigned int _hit_count;   ///< 缓存命中次数
    unsigned int _get_count;   ///< 缓存请求总次数
    std::string _file_name;    ///< 正在处理的跟踪文件名

    // ========== interval-hotness 相关字段（region 级统计，仅用于 eviction） ==========
    std::unordered_map<uint64_t, double> _region_hot;      ///< 每个 region 的热度 H
    std::unordered_map<uint64_t, uint64_t> _region_last_seq; ///< 每个 region 上次更新时的逻辑时间

    uint64_t _access_seq;      ///< 全局逻辑时间（这里直接使用 now_req 作为访问序号）

    // 这些名字在某些环境/库里容易与宏冲突（尤其是单字母 C），使用更安全的成员名避免 Release 配置下被宏污染
    uint64_t _region_size_blocks; ///< region 大小（以 block 数计）
    double _lambda;               ///< 衰减系数
    double _alpha;                ///< interval 权重放大系数
    double _smooth_c;             ///< 归一化平滑常数

    // 可更新最小堆：每个对象在堆内最多一个节点，避免 lazy entry 随访问次数膨胀
    UpdatableMinHeap _victim_heap;

private:
    /**
     * @brief 由对象 block_id 计算其所属 region id
     *
     * 当前工程中缓存 key 即 trace_line.starting_block+i，对应 SCORE 中 params.target，
     * 因此这里直接使用 block_id / REGION_SIZE_BLOCKS 进行划分。
     */
    uint64_t regionId(int block_or_object_id);

    /**
     * @brief 在每次访问时更新对应 region 的 interval-hotness
     *
     * dt = _access_seq - _region_last_seq[rid]
     * H  = H * exp(-LAMBDA * dt) + 1
     */
    void updateRegionHot(uint64_t rid);

    /**
     * @brief 计算给定 region 的 interval 权重（仅用于 eviction score 加权）
     *
     * 读取当前存储的 H 和 last_seq，在当前 _access_seq 上做一次指数衰减（不加 +1，因为非访问事件），
     * 然后进行归一化：
     *   norm = H / (H + C)
     *   return 1.0 + ALPHA * norm;
     */
    double wInterval(uint64_t rid);
};


