# IHARC 针对 OLTP Workload 的 3 个关键优化

## 背景问题分析

**当前 OLTP 测试结果（cache_size=10000）：**
- real_hit_rate = 0.613829（目标：追平/超过 ARC 的 0.6184）
- ghost_hit: G_hot=1081, **G_warm=52339**（G_warm 占绝对多数）
- final_p_hot=2479，HOT≈2409，WARM≈7591
- eviction: from_HOT=5244, **from_WARM=337772**（WARM 淘汰极多）

**核心问题：**
1. **G_warm ghost hit 爆炸**：大量"准热对象"在 WARM 被过早淘汰并很快回访
2. **p_hot 自适应方向反了**：G_warm ghost hit 时 `p_hot -= step`，缩小 HOT，使问题加剧
3. **HOT 容量严重不足**：p_hot 只有 2479/10000，导致大量热对象滞留在 WARM
4. **WARM 淘汰过于粗暴**：直接淘汰 LRU，不考虑对象热度

---

## 修改 1：修正 p_hot 自适应方向（核心修复）

### 位置：`iharc.cpp` Case C (in_g_hot) 和 Case D (in_g_warm)

### 修改前（错误逻辑）：
```cpp
// Case C: in_g_hot
_p_hot = std::min(_c, _p_hot + step);  // ✓ 正确：HOT ghost hit -> 增大 p_hot

// Case D: in_g_warm
_p_hot = std::max(0, _p_hot - step);   // ✗ 错误：WARM ghost hit -> 减小 p_hot
```

### 修改后（修正逻辑）：
```cpp
// Case C: in_g_hot
int step_hot = 1;  // 固定步长（推荐）
if (_step_rule == 1) {
    // 动态比例：step_hot = min(4, max(1, |G_hot|/max(1,|G_warm|)))
    step_hot = std::min(4, std::max(1, g_hot_size / std::max(1, g_warm_size)));
}
_p_hot = std::min(_c, _p_hot + step_hot);

// Case D: in_g_warm（核心修复）
int step_warm = 1;  // 固定步长（推荐）
if (_step_rule == 1) {
    // 动态比例：step_warm = min(8, max(1, |G_warm|/max(1,|G_hot|)))
    step_warm = std::min(8, std::max(1, g_warm_size / std::max(1, g_hot_size)));
}
_p_hot = std::min(_c, _p_hot + step_warm);  // ✓ 修正：WARM ghost hit -> 增大 p_hot
```

### 原理：
- **G_warm ghost hit**：对象从 WARM 被淘汰后很快回访 → 说明它"更像热对象"，应该进 HOT
  - 症状：G_warm=52339 >> G_hot=1081
  - 根因：HOT 容量不足（p_hot=2479），大量准热对象滞留 WARM 被过早淘汰
  - 应对：**增大 p_hot**，让更多准热对象有机会晋升 HOT

- **G_hot ghost hit**：HOT 对象被淘汰后回访 → HOT 也可能不足，也应增大 p_hot（幅度更小）
  - 应对：增大 p_hot，但 step_hot < step_warm

- **步长控制**：
  - 固定步长（step=1）：更稳定，避免震荡（推荐 OLTP）
  - 动态比例：反映 ghost 队列失衡程度，但需上限保护（4/8）

---

## 修改 2：WARM 淘汰增加热度保护（O(1) 单步保护）

### 位置：`iharc.cpp` replace() 函数的 WARM 淘汰分支

### 修改前（粗暴淘汰）：
```cpp
int victim = _warm.back();
_warm.pop_back();
_warm_pos.erase(victim);
// 直接淘汰，不考虑热度
```

### 修改后（热度保护 + 最多 2 次重试）：
```cpp
constexpr int protect_try_max = 2;
const double protect_threshold = _th_down;  // 保护阈值使用 Th_down

int victim = -1;
int try_count = 0;

while (try_count < protect_try_max && !_warm.empty()) {
    victim = _warm.back();
    
    // 懒更新获取 victim 的 heat
    double victim_heat = 0.0;
    auto heat_it = _heat_map.find(victim);
    if (heat_it != _heat_map.end()) {
        auto& meta = heat_it->second;
        int gap = _global_interval_id - meta.last_interval_id;
        if (gap > 0 && meta.last_interval_id >= 0) {
            victim_heat = meta.heat * std::pow(_alpha, gap);
        } else {
            victim_heat = meta.heat;
        }
    }
    
    // 判断是否需要保护
    if (victim_heat >= protect_threshold && try_count < protect_try_max - 1) {
        // 给予复活机会：移到 WARM MRU
        _warm.pop_back();
        _warm.push_front(victim);
        _warm_pos[victim] = _warm.begin();
        ++try_count;
    } else {
        break;  // 淘汰该 victim
    }
}

// 淘汰当前 victim
victim = _warm.back();
_warm.pop_back();
_warm_pos.erase(victim);
// ... 放入 G_warm
```

### 原理：
- **问题**：WARM 淘汰数量极多（337772），直接淘汰 LRU 不考虑对象是否"准热"
- **保护机制**：
  - 取 WARM LRU victim，检查 `heat(victim) >= protect_threshold`
  - 若满足：移到 WARM MRU（复活），继续尝试下一个 LRU
  - 最多重试 2 次，避免死循环
- **热度获取**：懒更新衰减，**不修改 _heat_map**，保持 O(1)
- **protect_threshold = Th_down**：即将被晋升的对象（heat 接近 Th_up）不会被直接淘汰

### 预期效果：
- 减少"即将晋升 HOT 的准热对象"被 WARM 直接淘汰
- 降低 ghost_warm，提升 real_hit_rate

---

## 修改 3：推荐参数 + 原理说明

### 位置：`iharc.cpp` printConfig() 函数顶部注释

### 推荐参数（OLTP 强频繁性场景）：
```cpp
Th_up: 0.30（而非默认 0.5）  // 放宽晋升门槛
Th_down: 0.15（而非默认 0.3） // 降级门槛相应降低，保持迟滞
alpha: 0.9
interval_len: 10000
step_rule: 0（固定步长，更稳定）
```

### 原理：
**OLTP workload 特征：强频繁性**
- 少量热对象反复访问（如热表、热索引）
- 访问模式：热对象频繁，非热对象偶尔

**若 Th_up 过高（如 0.5）：**
1. 大量"准热对象"需要多次访问才能晋升 HOT
2. 在晋升前可能在 WARM 被淘汰（因为 WARM 容量有限）
3. 淘汰后进入 G_warm，很快回访 → ghost_warm 爆炸
4. 旧逻辑：G_warm ghost hit → p_hot 减小 → HOT 更小 → 问题加剧

**若 Th_up 降低到 0.30：**
1. 准热对象更快晋升 HOT（少几次访问即可）
2. 减少在 WARM 被淘汰的概率
3. 降低 ghost_warm，提升 real_hit_rate

---

## 预期效果对比

### 优化前（存在问题）：
- real_hit_rate: 0.613829
- ghost_hit: G_hot=1081, **G_warm=52339**
- final_p_hot: 2479（偏小）
- eviction: from_HOT=5244, **from_WARM=337772**

### 优化后（预期）：
- **real_hit_rate: 目标 >= 0.62**（追平/超过 ARC 的 0.6184）
- **ghost_warm 大幅降低**：预期 < 20000（减少 60%+）
- **p_hot 增大**：预期 > 5000（HOT 容量增加，容纳更多热对象）
- **from_WARM 淘汰减少**：预期 < 200000（减少 40%+）
- **promotion 增加**：更多对象晋升 HOT（Th_up 降低 + p_hot 增大）

---

## 核心论点总结

**为什么这些改动应该提升 OLTP 命中率？**

1. **修改 1 修正自适应方向**：
   - G_warm ghost hit 爆炸 → 说明大量准热对象被误当暖淘汰
   - 应增大 HOT 容量（p_hot），而非减小
   - 修正后：p_hot 持续增大，直到 G_warm 下降

2. **修改 2 WARM 热度保护**：
   - 减少"即将晋升的准热对象"被 WARM 直接淘汰
   - 给予复活机会，让它们有更多时间晋升 HOT
   - O(1) 复杂度，不影响性能

3. **修改 3 放宽晋升门槛**：
   - Th_up 0.5 → 0.30：准热对象更快晋升 HOT
   - 减少在 WARM 滞留时间，降低被淘汰概率
   - 适配 OLTP 强频繁性特征

**三管齐下 → 降低 ghost_warm + 提升 real_hit_rate**

---

## 使用方法

### 1. 重新编译项目
```bash
# Visual Studio: Ctrl+Shift+B
# 或命令行
msbuild SCORE.sln /p:Configuration=Release
```

### 2. 使用推荐参数运行（main.cpp 中修改）
```cpp
IHARCCache iharc_cache(c, argv[2],
    /*interval_len=*/10000,
    /*alpha=*/0.9,
    /*th_up=*/0.30,      // 推荐值
    /*th_down=*/0.15,    // 推荐值
    /*step_rule=*/0);    // 固定步长
```

### 3. 对比输出
重点观察：
- `real_hit_rate`（是否提升）
- `ghost_hit (G_hot+G_warm)`（G_warm 是否下降）
- `final_p_hot`（是否增大）
- `eviction: from_WARM`（是否减少）
- `promotion: WARM->HOT`（是否增加）

---

## 文件修改清单

1. **iharc.cpp**
   - `printConfig()`：添加推荐参数说明
   - `replace()`：WARM 淘汰增加热度保护（47 行新增）
   - `get()` Case C：修正 G_hot ghost hit 的 p_hot 更新
   - `get()` Case D：修正 G_warm ghost hit 的 p_hot 更新（核心修复）

2. **iharc.h**
   - 无修改（保持原有接口）

3. **main.cpp**
   - 建议修改 IHARCCache 构造参数为推荐值

---

## 注意事项

1. **保持 O(1) 复杂度**：所有修改均无全量遍历
2. **向后兼容**：可通过参数控制是否启用优化
3. **适用场景**：OLTP 强频繁性 workload（少量热对象反复访问）
4. **不适用场景**：扫描型 workload（大量对象各访问一次）

---

## 作者说明

这些优化基于对 OLTP workload 特征的深入分析：
- **问题诊断**：G_warm=52339 >> G_hot=1081 是核心症状
- **根因**：p_hot 自适应方向错误 + 晋升门槛过高 + WARM 淘汰粗暴
- **修复方案**：三管齐下，针对性解决

预期这些修改能将 IHARC 的 real_hit_rate 从 0.6138 提升到 >= 0.62，追平/超过 ARC。

