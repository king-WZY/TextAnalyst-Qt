# TextAnalyst-Qt 详细设计文档（Detailed Design Specification）

> 文档版本：v1.2（2026-08-29 UI 行为修订版）
> 上游文档：`docs/architecture/01-blueprint.md`（v1.0 蓝皮书，下称 **BLUEPRINT**）
> 上游文档：`docs/architecture/02-system-architecture.md`（v1.0.1-ubuntu，下称 **ARCH-UB**）
> 平台范围：**Ubuntu 22.04 / 24.04 LTS**（20.04 仅维护态）
> 技术栈：**C++17 + Qt 6.4+（Widgets）**
> 架构：**4 层（Presentation / Controller / BLL / DAL）**
> 文档状态：**v1.0 已评审，v1.1 为修订版**

> 修订记录（v1.0 → v1.1，对应评审问题单）：
> - **R-01** `std::atomic<std::shared_ptr<T>>` 是 C++20 特性，C++17 不可编译 → 引入 `SharedSnapshot<T>`（§1.5）。
> - **R-02** `kMaxRules=128` 与 `ruleRef` 6 bit（上限 64）矛盾 → 统一为 64（§1.1）。
> - **R-03** `LineMeta::valid()` 语义与空行冲突 → 删除（§1.3）。
> - **R-04** `EncodingDetector::detect` 全流程与"索引只跑 L1+L2"矛盾 → 新增 `detectFast()`（§2.3）。
> - **R-05** `TextBuffer::create` 返回类型歧义 → 明确为 `shared_ptr<TextBuffer>` 工厂（§2.5）。
> - **R-06** 空文件语义矛盾（rowCount 0 vs 1）→ 统一为空文件 rowCount()==0（§2.5/§10.2）。
> - **R-07** `toUtf8` 注释引用已删除的 `LineMeta.hash` → 修正为按行号/偏移缓存。
> - **R-08** 内存预算 ≤1.1 GiB 与 mmap+LineMeta 分解矛盾 → 修订为 ≤1.4 GiB（含全文件访问最坏情形，§9）。
> - **R-09** `MainController::onLineActivated` 缺失 → 补充槽声明（§6.2）。
> - **R-10** `RuleSet::matches` 未落地 → 删除（匹配归 `FilterEngine::classifyLine`）。
> - **R-11** 拼写修正：`kMarkerArbg`→`kMarkerArgb`、`adviseSequencial`→`adviseSequential`。
>
> 修订记录（v1.1 → v1.2，用户人工确认反馈）：
> - **R-13** 过滤器面板改为原版 TAT 三行布局：Filter/Text Color/Background
>   三个下拉框 + Text 输入行 + Description 备注行 + 底部三复选框
>   （Excluding [!] / Case-sensitive [Aa] / Regular expression [R]）。
> - **R-14** 双击行行为：预填 Text 输入框（可编辑）而非隐式把行转成规则
>   应用——旧行为导致该行被 Exclude 隐藏、显示为与背景同色的空白（用户
>   反馈"双击后行变白不可见"）。
> - **R-15** 行号列移到文本左侧（原实现右侧显示）。
> - **R-16** 过滤面板置于窗口底部（上下两部分布局），View 菜单提供
>   toggle 动作，面板关闭后可恢复。
> - **R-17** 状态栏左侧固定显示文件完整路径（不再被临时提示消息覆盖），
>   操作反馈移至右侧 permanent 标签。
> - **R-18** `FilterRule` 扩展 `matchType`（Contains/Exact/StartsWith/
>   EndsWith）与 `description` 字段；`.tat` 增加可选属性 `match`/`desc`
>   （向后兼容，缺省 = Contains）。
>
> 修订记录（v1.2 追加，二轮人工确认反馈）：
> - **R-19** 规则求值语义改为【列表顺序优先】（原版 TAT）：第一条命中的
>   规则决定行命运（Excluding → Hidden，着色 → Highlighted），无命中 →
>   Normal。旧"include 全部优先 + 未命中 include 判 Dimmed"两轮遍历废止
>   （Dimmed 不再产生；添加一条 Include 规则后整屏变灰的观感问题根除）。
> - **R-20** Dock 布局状态版本化（`v2|` 前缀）：旧版本会话保存的右侧
>   dock 布局不再覆盖新的底部布局（问题 3"看起来没修复"的根因）。
> - **R-21** 行号列宽按总行数位数自适应（48–112 px）；新规则默认前景色
>   Red（着色效果立即可见）。
> - **R-22** 过滤规则编辑弹窗化（三轮人工确认）：编辑表单不再常驻面板——
>   双击日志行弹出"添加过滤规则"对话框（预填该行文本），双击规则列表
>   中的条目弹出**同一对话框**的"修改"模式，确认/取消后销毁，不占空间。
>   实现为 `ui/dialogs/FilterRuleDialog`（纯编辑对话框，与列表/控制器
>   零耦合）；底部面板瘦身为紧凑规则列表（添加/删除 + 双击编辑）；
>   销毁协议用 `finished → deleteLater`（WA_DeleteOnClose 在 open()+
>   close() 组合下部分平台不触发，offscreen 实测确认）。
> - **R-23** 规则操作入口收敛到菜单栏 **Filters 菜单**（原版 TAT 八项），
>   面板上的"添加/删除"按钮移除——面板为纯列表：
>   Previous Match（Shift+F8）/ Next Match（F8）在着色规则命中的行间
>   循环跳转（按规则指纹+结果世代缓存失效）；Add New Filter...（Ctrl+N）
>   弹添加窗；Edit/Remove Selected Filter 作用于面板选中行（无选中时
>   菜单项自动禁用）；Enable/Disable All 只切 isEnabled 保留规则；
>   Remove All 清空列表。
> - **R-24** 视图 / Filters 菜单并入顶部单行工具条（QToolButton +
>   InstantPopup 置于工具条最左），废除独立菜单栏的两行标题结构。
> - **R-25** 工具条排序 [打开][保存规则][加载规则] │ [视图▾][Filters▾] │
>   [导出可见][查找]；换文件时清空委托转码缓存/搜索命中缓存/F8 匹配
>   缓存并清除选中回到首行——缓存按行号为 key，不清空会让新文件行命中
>   旧文件文本缓存，观感为"新文件追加在旧文件后面"。

本文档的职责是把 BLUEPRINT 与 ARCH-UB 中的**约束**推导为**可编码**的实现细节：类接口（含线程安全语义）、
核心算法（含复杂度与终止条件）、双缓冲与取消协议、序列化格式、错误码、测试矩阵。
文中不重复 ARCH-UB 已收敛的平台决策，只在其基础上做细化；两者冲突处以本文档 §0.4 裁决表为准。

---

## 0. 导读与基线

### 0.1 术语

| 术语 | 含义 |
| :--- | :--- |
| DAL / BLL / UI / CTL | 数据访问层 / 业务逻辑层 / 表现层 / 控制层 |
| `TextBuffer` | 只读文本快照，由 mmap 基址 + 行索引构成，是 DAL 与 BLL 的唯一交接物 |
| `LineMeta` | 单行的元数据（字节偏移 + 字节长度），不含文本内容本身 |
| `ResultState` | 单行最终渲染状态（2 位）+ 颜色索引，由 `FilterEngine` 产出 |
| FrontBuffer / BackBuffer | 双缓冲中的"渲染中"与"计算中"两个 `FilterResult` 快照 |
| 快照（Snapshot） | 通过原子操作取得的 `std::shared_ptr<const FilterResult>`，读者私有持有 |
| `Token` | 取消令牌（`{std::atomic<int> gen; int myGen;}`），用于让过期任务自我终止 |
| XDG | X Desktop Group 规范，用户目录布局（`XDG_CONFIG_HOME` 等） |
| TAT | 原版 TextAnalyst（Delphi 实现），本项目保持 `.tat` 文件格式互通 |

### 0.2 关键性能目标（与 ARCH-UB §13 一致，此处给出推导）

两个预算是**独立场景**，不是同一文件同时满足两个条件：

| 场景 | 假设 | 目标 |
| :--- | :--- | :--- |
| P1 大文件 | 1 GiB 文件，约 1000 万–5000 万行 | 打开 < 500 ms；峰值内存 ≤ 1.4 GiB（全文件访问后；典型视口 < 350 MB，§9.2） |
| P2 多行文件 | 约 1 亿行，约 1–2 GiB 原始文本 | 索引 < 2 s；标记跳转 < 1 ms；全量正则 < 3 s（可取消） |

> 推导说明：1 GiB / 1 亿行 = 10 字节每行，是"一行一条日志"的极端压缩场景，实际日志罕见。
> 若强行按此组合推算，`LineMeta` 自身即占用 800 MB，因此本文明确把两者拆成 P1 / P2 两个场景，
> 并在 §9.2 按场景给出内存分解。
> **R-08 说明**：ARCH-UB §13 的"≤1.1 GiB"未计入 mmap 本身与 `LineMeta`/`FilterResult` 结构体，
> 修订为 ≤1.4 GiB（最坏情形：文件被完整滚动一遍后 mmap 页全部驻留）。

### 0.3 硬约束（违反即代码评审退回）

1. **BLL 层不依赖 Qt**：`core/engine/` 下所有 `.cpp` 不得 `#include` Qt 头文件。颜色以 `uint32_t` ARGB 传入。
2. **DAL 层只用 POSIX**：只允许 `sys/mman.h`、`fcntl.h`、`sys/stat.h`、`sys/inotify.h`、`unistd.h`、
   `sys/mman.h` 的 `madvise`、`iconv.h`、`pthread.h`。**禁止** `QFile::map`、Windows/macOS API。
3. **GUI 主线程不做 I/O、正则、mmap 系统调用**（ARCH-UB §8 MUST）。
4. **索引层一律 UTF-8 字节偏移**：任何编码的显示转换只发生在渲染阶段（`LogViewDelegate`）。
5. **路径一律经 `QStandardPaths`**：禁止硬编码 `~/.config` 之类路径。
6. **`.tat` 格式与原版 TAT 互通**：字段集、属性名、大小写不得变更。
7. **无锁读**：渲染线程读取 `TextBuffer` 与 `FilterResult` 时不得加锁，只通过原子发布/快照。

### 0.4 BLUEPRINT 与 ARCH-UB 冲突裁决

| # | 议题 | BLUEPRINT | ARCH-UB | **本文档决策** | 理由 |
| :-: | :--- | :--- | :--- | :--- | :--- |
| 1 | 内存映射 API | `QFile::map()` | 原生 `mmap()` | **原生 `mmap()`** | 避免 `QFile` 生命周期耦合；`madvise` 语义直接可控 |
| 2 | `ResultState` 存储 | `vector<ResultState>` 每行 1 字节 | `atomic<vector<quint8>*>` | **`vector<uint8_t>`，每行 1 字节**（状态 2 bit + 颜色索引 6 bit） | 1 亿行仅 100 MB；双缓冲发布改用 `shared_ptr` 快照（§5） |
| 3 | 颜色存储 | 隐含在 `FilterRule` 中 | 未定义 | **`ruleColorRef` 位集**，颜色按 rule id 查表 | 省 400 MB/亿行（§9.2） |
| 4 | MarkerManager 容器 | `unordered_set<int>` | MUST `unordered_set<int>` | **`std::array<std::vector<int>,8>`（有序）** | `unordered_set` 无法保证 `next()` 得到"下一个更大行号"；有序 vector 使查找与跳转均为 O(log n)，满足 < 1 ms |
| 5 | 编码检测 | `QStringConverter` 探测 | BOM → iconv → ICU → local8Bit | **ARCH-UB 四级** | 蓝皮书单级探测对 GB18030 误判率高 |
| 6 | 配置路径 | `~/.config/MyTextAnalyst.conf` | `~/.config/textanalyst-qt/` | **`~/.config/textanalyst-qt/textanalyst-qt.conf`** | 遵循 XDG + 应用命名空间 |
| 7 | 标记快捷键 | 数字键 `3` 跳转 | `Ctrl+1..8` 切换 / `Alt+1..8` 跳转 | **`Ctrl+1..8` 切换，`Alt+1..8` 跳转** | 与 ARCH-UB 一致，且不与搜索框输入冲突 |
| 8 | QtXml 链接 | `QXmlStreamWriter` | 同 | **`Qt6::Xml` 需显式链接** | Qt 6 将 XML 拆为独立库 |
| 9 | 隐藏行显示 | `setOpacity(0)` 或不绘制 | 同 | **不绘制内容，但保留行槽与行号** | 保持原始行号对齐（§7.3） |
| 10 | 任务取消 | `QAtomicInt m_cancel` + `waitForDone(500ms)` | 同 | **`Token` 世代号** + 无 `waitForDone` | 见 §5.4 |

### 0.5 文档约定

- `bool` 参数默认值写在声明处；MUST / SHOULD 沿用 ARCH-UB §0 语义。
- 所有 `offset` / `length` 单位为**字节**，指向 mmap 基址的相对偏移。
- 行号为 **1-based**（与原版 TAT 及 `/Line:` 语法一致），内部索引为 0-based。
- 线程标注约定：`[M]` 主线程，`[W]` 工作线程，`[A]` 任意线程。

---

## 1. 基础类型与数据模型

### 1.1 类型别名

```cpp
// src/core/models/common.h
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace tat {

using Offset  = uint32_t;   // mmap 基址内的字节偏移（P1/P2 场景下 < 4 GiB）
using Length  = uint32_t;   // 行字节长度，不含行结束符
using LineNo  = int;        // 1-based，-1 表示无效
using RuleId  = int;
using Hash32  = uint32_t;

// ARGB，高位 A 在前；与 QColor::rgb() 兼容（Qt 内部同样是 #AARRGGBB）
using Argb    = uint32_t;

inline constexpr LineNo  kInvalidLine = -1;
inline constexpr int     kMarkerCount = 8;
// 同时启用的规则上限。ruleRef 只有 6 bit（0..63，见 §1.5），超出时应用被拒绝并提示。
inline constexpr int     kMaxRules    = 64;
inline constexpr Argb    kMarkerArgb[kMarkerCount] = {
    0xFF22AA55, 0xFFAA3333, 0xFF3366CC, 0xFF9933CC,
    0xFFCC6600, 0xFF33AA33, 0xFF7F8C8D, 0xFF555555,
};

} // namespace tat
```

### 1.2 枚举

```cpp
enum class FilterAction : uint8_t { Include = 0, Exclude = 1 };

enum class MatchMode : uint8_t { Substring = 0, Regex = 1 };
// BLUEPRINT 中的 Marker 模式不成立：标记是"行状态"而非"匹配模式"，
// 已由 MarkerManager 承担，故此处不保留第三个取值。

enum class LineEnd : uint8_t { LF, CRLF, CR, LastNoEol };

// 渲染状态，2 bit。见 §3.2.2 的状态决策表。
enum class ResultState : uint8_t {
    Normal     = 0,   // 未被任何规则命中
    Hidden     = 1,   // 被排除，不绘制（行槽保留）
    Dimmed     = 2,   // 非白名单，降低对比度绘制
    Highlighted = 3,  // 命中高亮
};

enum class Encoding : uint8_t {
    Unknown, Utf8, Utf8Bom, Utf16Le, Utf16LeBom, Utf16Be, Utf16BeBom,
    Gbk, GB2312, GB18030, Cp932, Cp936, Cp950, Cp1252,
    Local8Bit,   // 由 LANG/LC_ALL 决定的单字节编码
};

enum class MemoryMapFlags : uint8_t { ReadOnly = 0, ReadWrite = 1 };
```

### 1.3 行元数据

**决策（§0.4 #2/#3）**：`LineMeta` 固定 8 字节，`offset` + `length`，**不含 hash**。
原版 BLUEPRINT 的 `hash` 字段用途是"快速去重或缓存标识"，但去重功能在 v1.0 未排期，
保留它会令 P2 场景的索引占用从 800 MB 升到 1.2 GB，直接击穿内存预算。
hash 作为**可选追加数组**保留，见 §1.3.2。

```cpp
// src/core/buffer/LineMeta.h
#pragma once
#include <cstdint>
#include <cstring>
#include "common.h"

namespace tat {

// 8 字节紧凑表示。1 亿行 = 800 MB。
// 不变式：offset + length <= mappedSize；length 不含行结束符。
// 空行（length == 0）是合法行（如 "\n\n"），用 empty() 判断而非 valid()。
// 索引器保证 offset/length 在界内，因此不提供 valid()（R-03）。
struct alignas(8) LineMeta {
    Offset offset = 0;
    Length length = 0;

    bool   empty()  const noexcept { return length == 0; }
    size_t paddedSize() const noexcept { return sizeof(LineMeta); }
};

static_assert(sizeof(LineMeta) == 8, "LineMeta must stay cache-line friendly");

// 分段索引用：offset 提升到 64 位，覆盖 > 4 GiB 单段。仅当 mmap 段超限时启用。
struct alignas(8) SegmentMeta {
    uint64_t offset = 0;
    Length   length = 0;
};

static_assert(sizeof(SegmentMeta) == 16, "SegmentMeta layout");

} // namespace tat
```

#### 1.3.1 对齐说明

若把三字段写成 `uint32_t offset; uint32_t length; uint32_t hash;`，结构体大小为 12 字节；
`std::vector` 内部不做额外对齐，但 `LineMeta*` 元素步长为 12，会破坏 8 字节缓存行边界对齐，
并使 SIMD 友好的批量处理失效。因此 v1.0 采用 8 字节形态。

#### 1.3.2 可选 hash 附加数组

```cpp
// 仅在启用 --dedup 或行去重视图时构建，独立于 LineMeta 存放。
// hash[i] = FNV-1a(mmap[i.length 字节]) ^ (uint32_t)i
// 目的：让 FNV 相同的多行可共享缓存（渲染字形布局缓存的键）。
// 内存代价：+4 B/行，P2 场景 +400 MB，故默认关闭。
std::vector<Hash32> buildLineHashes(const LineMeta* meta, const char* base, int count,
                                    const std::vector<size_t>& sampleIndices);
```

### 1.4 错误模型

```cpp
// src/core/models/Error.h
#pragma once
#include <string>

namespace tat {

enum class ErrCode : int {
    Ok                  =  0,
    InvalidArgument    = -1,
    FileNotFound       = -2,
    PermissionDenied   = -3,
    MapFailed          = -4,
    OutOfMemory        = -5,
    EncodingUnknown    = -6,
    RegexError         = -7,
    XmlError           = -8,
    Cancelled          = -9,
    Timeout            = -10,
    IoError            = -11,
    UnsupportedFormat  = -12,
};

// 不可变、可拷贝、可跨线程传递。BLL/DAL 内部用它，不抛异常（见 §8.3）。
struct Error {
    ErrCode      code     = ErrCode::Ok;
    std::string  op;        // 出错操作名，如 "mmap"、"compileRegex"
    std::string  message;   // 人类可读，用于日志；不上 UI（UI 走 i18n）

    bool   ok()   const noexcept { return code == ErrCode::Ok; }
    bool   hasError() const noexcept { return code != ErrCode::Ok; }
    static Error none() noexcept { return {}; }
};

} // namespace tat
```

> 决策：BLL/DAL **不使用 C++ 异常**，统一返回 `Error` 或 `Error + 出参`。
> 理由：`QtConcurrent::run` 中未捕获异常会让 `QThreadPool` 状态难以界定；
> 返回值模型下，错误码可被 §11 的崩溃日志与 `--verbose` 直接落盘。
> Qt 自身的 `QRuntimeException` 仅在 UI 层（Qt 对象断言失败）出现，由 `QCoreApplication::installMessageHandler` 统一收敛。

### 1.5 任务令牌与进度

```cpp
// src/core/models/Task.h
#pragma once
#include <atomic>
#include <cstdint>
#include <vector>
#include "common.h"

namespace tat {

// 单调递增的世代号。取消 = 自增；过期任务观察到世代变化后自行退出。
class Token {
public:
    void cancel() noexcept { gen_.fetch_add(1, std::memory_order_release); }
    bool valid(int myGen) const noexcept {
        return gen_.load(std::memory_order_acquire) == myGen;
    }
    int current() const noexcept { return gen_.load(std::memory_order_relaxed); }
private:
    std::atomic<int> gen_{0};
};

// 任务私有世代快照，避免多次 load。
struct TokenSnapshot {
    Token*  token = nullptr;
    int     myGen = 0;
    bool    valid() const noexcept { return token && token->valid(myGen); }
};

// —— C++17 兼容的无锁发布-订阅快照（R-01）——
// std::atomic<std::shared_ptr<T>> 是 C++20 特性，C++17 不可编译。
// 本类基于 C++11 的 std::atomic_load/atomic_store 自由函数（libstdc++ 内部
// 全局自旋锁，无争用时单次 ~20-40 ns 可接受），对外语义与 C++20 版一致：
//   写者 publish() 原子替换；读者 snapshot() 取得私有快照，
//   旧对象在最后一个快照析构时才释放（无 use-after-free，见 §5.4）。
// 所有层一律使用本类，禁止直接写 std::atomic<std::shared_ptr<T>>。
template <class T>
class SharedSnapshot {
public:
    SharedSnapshot() = default;
    SharedSnapshot(const SharedSnapshot&) = delete;
    SharedSnapshot& operator=(const SharedSnapshot&) = delete;

    void publish(std::shared_ptr<T> value) noexcept {
        std::atomic_store(&m_slot, std::move(value));
    }
    std::shared_ptr<T> snapshot() const noexcept {
        return std::atomic_load(&m_slot);
    }
    void clear() noexcept { publish(nullptr); }
private:
    mutable std::shared_ptr<T> m_slot;   // 仅经 atomic 自由函数访问
};

// 用法：
//   SharedSnapshot<const FilterResult> resultStore;   // §5.4
//   SharedSnapshot<const TextBuffer>   bufferStore;   // §5.5

struct Progress {
    double   percent      = 0.0;   // 0.0 – 100.0
    size_t   current      = 0;
    size_t   total        = 0;
    uint64_t bytesDone    = 0;
    uint64_t totalBytes   = 0;
    uint64_t elapsedMs    = 0;
    bool     isFinished() const noexcept { return total > 0 && current >= total; }
};

// 每行最终结果，1 字节。
//   bit 0..1 : ResultState
//   bit 2..7 : ruleColorRef —— 高亮时对应 FilterRule.id 的索引（0..63）；
//                          Dimmed 时指向"白名单首条规则"（用于下划线色）；
//                          Normal/Hidden 时为 0。
// 颜色不在此处存放，见 §3.3 的 ruleColorRef 设计。
struct alignas(1) RowState {
    uint8_t raw = 0;

    ResultState state() const noexcept { return static_cast<ResultState>(raw & 0x3); }
    int         ruleRef() const noexcept { return (raw >> 2) & 0x3F; }
    bool        isHidden()  const noexcept { return state() == ResultState::Hidden; }
    bool        isNormal()  const noexcept { return state() == ResultState::Normal; }
    bool        hasColor()  const noexcept {
        return state() == ResultState::Highlighted || state() == ResultState::Dimmed;
    }
    static uint8_t make(ResultState s, int ruleRef) noexcept {
        return static_cast<uint8_t>((static_cast<uint8_t>(s) & 0x3)
                                   | ((static_cast<uint8_t>(ruleRef) & 0x3F) << 2));
    }
};
static_assert(sizeof(RowState) == 1, "one byte per line");

} // namespace tat
```

### 1.6 过滤规则

```cpp
// src/core/models/FilterRule.h
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include "common.h"

namespace tat {

// 值语义、可拷贝。规则集在 Apply 期间不可变（见 §5.4 的快照发布）。
struct FilterRule {
    int           id              = -1;   // 由 FilterEngine 分配，删除后不复用
    FilterAction  action          = FilterAction::Exclude;
    MatchMode     mode            = MatchMode::Substring;
    FilterMatch   matchType       = FilterMatch::Contains;  // R-18：匹配锚定
    std::string   pattern;                        // 原始输入，序列化用
    std::string   description;                    // R-18：备注（仅展示/序列化）
    std::optional<std::string> compiled;          // Regex 模式下的编译结果（见 §4.1）
    Argb          foreground      = 0xFF000000;   // #000000
    Argb          background      = 0xFFFFFFFF;   // #FFFFFF（透明背景由 UI 层解释）
    bool          caseSensitive   = false;
    bool          isEnabled       = true;
    bool          wholeWord       = false;         // 词边界；仅 Substring 生效
    int           matchCount      = 0;             // 运行态，不序列化
    int           rank            = 0;             // Dock 中显示顺序
};

// 规则快照：Apply 期间只读，多 worker 共享，零锁。
// 由 FilterEngine 在提交任务时构造并 shared_ptr 发布（§5.4）。
// 匹配逻辑不属于数据：由 FilterEngine::classifyLine + 注入的 MatchFn 完成（R-10）。
struct RuleSet {
    std::vector<FilterRule> rules;                    // 已过滤 isEnabled
    std::vector<int>        ruleIds;                  // 与 rules 同序的 id
    std::vector<std::string> compiled;                // Regex 编译结果
    bool                    hasInclude = false;
    bool                    hasExclude = false;
    bool                    anyRule    = false;
    uint64_t                fingerprint = 0;          // 规则集指纹，用于缓存失效判断
    int                     generation = 0;           // 与 Token 对齐的世代号
};

} // namespace tat
```

> **决策**：BLUEPRINT 的 `QRegularExpression compiledRegex` 成员会引入 Qt 依赖，
> 违反 §0.3 硬约束 1。v1.0 改为 `std::optional<std::string> compiled` 存放**编译产物的不透明字节**，
> 由 `Searcher`/`FilterEngine` 的 `BllContext` 注入回调完成实际编译（§4.1）。
> 这样 BLL 头文件零 Qt 依赖，同时保留"预编译"性能收益。

### 1.7 标记

```cpp
// src/core/models/Marker.h
#pragma once
#include <cstdint>

namespace tat {

// markerId: 1..8（1-based，对应 Ctrl+1..8）
// 存储容器为 std::array<std::vector<int>, kMarkerCount>，
// 每个 vector 按 line 严格升序（§3.4）。
struct Marker {
    int line = 0;
    int markerId = 0;
};

} // namespace tat
```

### 1.8 编码结果

```cpp
// src/core/buffer/EncodingInfo.h
#pragma once
#include <cstdint>
#include <string>
#include "common.h"

namespace tat {

// EncodingDetector 的产物。TextBuffer 持有；渲染时按需转码（§7.3）。
struct EncodingInfo {
    Encoding         encoding  = Encoding::Utf8;
    int              bomLen    = 0;            // BOM 占用的字节数，索引时跳过
    std::string      codecName = "UTF-8";     // iconv/ICU 使用的名称，如 "GB18030"
    double           confidence = 1.0;         // 0.0 – 1.0
    bool             isUtf8ByteStream() const noexcept {
        return encoding == Encoding::Utf8 || encoding == Encoding::Utf8Bom;
    }
};

} // namespace tat
```

---

## 2. 数据访问层（DAL）

### 2.1 模块职责与线程模型

| 类 | 职责 | 构造 | 析构 | 并发语义 |
| :--- | :--- | :--- | :--- | :--- |
| `MemoryMappedFile` | `open/flock/mmap/madvise/stat` | 任意线程 | 任意线程（幂等） | 对象生命周期由 `shared_ptr` 管理；`data()` 指针在析构前稳定 |
| `EncodingDetector` | 编码识别（纯函数） | 任意线程 | — | 无状态，可并发调用 |
| `LineIndexer` | 扫描换行符，产出 `LineMeta` | 任意线程 | — | 输入只读，输出写入调用方独占的 `vector` |
| `TextBuffer` | 对外只读查询接口 | 工作线程构建 | 主线程 | `lines()`/`lineAt()`/`textAt()` 无锁读；`mmap` 变更由 `MemoryManager` 单线程驱动 |
| `FileWatcher` | `inotify` 监听单文件 | 主线程 | 主线程 | fd 在主线程创建，事件通过 `QMetaObject::invokeMethod` 回主线程 |

**线程安全总则（MUST）**：

- `TextBuffer` 的 `LineMeta` 数组与 `const char* basePtr` 一旦发布，**永不原地修改**。
  文件重载时构造**新的** `TextBuffer`，用原子指针替换旧对象（引用计数归零后旧对象在任意线程安全析构）。
- `MemoryManager` 对 `mmap` 段的 `madvise(MADV_DONTNEED)` 会破坏 `TextBuffer` 的数据不变式
  （"任意行随时可读"），因此 v1.0 **禁用** `MADV_DONTNEED` 自动卸载，改为在 §2.2.4 中定义
  受控卸载协议，默认关闭，仅作为 `--compact-memory` 实验开关。
  > 这是对 ARCH-UB §3.1.3 的收敛：该条在蓝皮书与 ARCH-UB 中都属于"应对内存压力"的兜底手段，
  > 但它与"渲染线程无锁读任意行"直接冲突。v1.0 选择牺牲自动卸载以换取渲染不变式；
  > 触发条件（`MemAvailable < 15%`）改为在状态栏告警并提示用户关闭文件。

### 2.2 `MemoryMappedFile`

#### 2.2.1 接口

```cpp
// src/core/buffer/MemoryMappedFile.h
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "common.h"
#include "Error.h"

namespace tat {

class MemoryMappedFile {
public:
    MemoryMappedFile() = default;
    ~MemoryMappedFile();
    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    MemoryMappedFile(MemoryMappedFile&& o) noexcept;
    MemoryMappedFile& operator=(MemoryMappedFile&& o) noexcept;

    // 打开并映射。失败时 isValid()==false，error 描述原因。
    static MemoryMappedFile open(const std::string& path, MemoryMapFlags flags, Error* error);

    // 追加打开（导出模式，§6.2 用）。
    static MemoryMappedFile createOrTruncate(const std::string& path, std::size_t preAllocBytes, Error* error);

    bool       isValid()   const noexcept { return fd_ >= 0 && data_ != nullptr; }
    const char* data()     const noexcept { return static_cast<const char*>(data_); }
    size_t     size()      const noexcept { return static_cast<size_t>(size_); }
    uint64_t   mtimeNs()   const noexcept { return mtimeNs_; }
    bool       isLocked()  const noexcept { return locked_; }

    // 内核提示。返回 false 表示 madvise 不支持该 flag，不视为错误。
    bool adviseRandom()   noexcept;   // MADV_RANDOM
    bool adviseNormal()   noexcept;   // MADV_NORMAL
    bool adviseSequential() noexcept; // MADV_SEQUENTIAL
    bool adviseDontNeed(const char* range, size_t len) noexcept; // 实验特性，默认禁用

    // 外部变更检测（inotify 触发后调用，非阻塞）。
    uint64_t statMtimeNs() const noexcept;
    bool     sizeChanged() const noexcept;   // 与 size_ 比较

private:
    void closeNoThrow();
    bool acquireReadLock(Error* error);

    void*   data_   = nullptr;
    size_t  size_   = 0;
    int     fd_     = -1;
    bool    writable_ = false;
    bool    locked_   = false;
    uint64_t mtimeNs_ = 0;
};

} // namespace tat
```

#### 2.2.2 打开算法

```
open(path, ReadOnly):
  1. canonical = realpath(path);            失败 → ErrCode::FileNotFound
  2. fd = open(canonical, O_RDONLY | O_CLOEXEC); 失败 → PermissionDenied
  3. st = fstat(fd);
     if st.st_nlink == 0 → Ok（合法：已被 unlink 但仍持有 fd）
     if st.st_size == 0 → 返回空映射（size_ = 0，data_ = nullptr，isValid()==true）
  4. statx(fd, {}, AT_STATX_SYNC_AS_STAT, STATX_MTIME, &sxb);
     mtimeNs_ = sxb.stx_mtime.tv_sec * 1e9 + sxb.stx_mtime.tv_nsec
  5. fcntl(fd, F_SETLK, {F_RDLCK, SEEK_SET, 0, st.st_size})
     失败（EACCES/EAGAIN）→ locked_ = false，附加 Warning，**不失败**
     成功 → locked_ = true
  6. data_ = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0)
     MAP_FAILED → ErrCode::MapFailed
  7. madvise(data_, st.st_size, MADV_RANDOM)     // 日志是随机访问模式
     失败 → 忽略（不降级）
  8. close(fd)  // mmap 后 fd 可关闭，减少 fd 表压力
  9. return this
```

**关键点**：

- `MAP_PRIVATE`：即使外部文件被修改，映射区域仍保持加载时快照。与 TAT 原版"快照"策略一致。
- 步骤 8 关闭 fd：`mmap` 持有独立引用，关闭 fd 不影响映射有效性，且释放 `max_user_fds` 配额
  （Ubuntu 默认 `max_user_watches` 之外，`fs.file-max` 在容器里常受限）。
- `O_CLOEXEC`：避免 `fork` 后子进程继承（本程序不 fork，但防御性设置）。
- 空文件特例：`mmap(len=0)` 在部分内核版本返回错误；显式走空映射分支，`isValid()==true`
  且 `data()==nullptr`，调用方必须判空。

#### 2.2.3 `fcntl` 读锁语义

- 使用 `F_SETLK`（非阻塞）而非 `F_SETLKW`：不等待，立即返回。
- 只提示不失败：若加锁失败，UI 状态栏显示"文件被其他进程写入中"，加载继续。
- 锁在 `mmap` 完成后**立即释放**（`closeNoThrow()` 内的 unlock），因为 `MAP_PRIVATE` 已经与源文件解耦，
  保持锁的意义只剩"提示"。若需长时间持锁，改在 `open` 返回后由调用方显式 `holdLock()`。
  > 决策说明：ARCH-UB §3.1.1 只说"加读锁"，未说明持锁时长。长期持锁会与日志追加者形成互斥，
  > 因此 v1.0 采用"采样式"语义：只在打开瞬间采样一次，避免与 `tail -f` 类工具冲突。

#### 2.2.4 受控卸载协议（实验特性，默认关闭）

```
启用条件：命令行 --compact-memory 或 Settings key memory/compactMode == true
前提：TextBuffer 已发布，无正在进行的 Apply/索引任务

流程（MemoryManager 后台定时器，周期 5 s）：
  1. 读 /proc/meminfo 的 MemAvailable；阈值 = 总内存 * 0.15
  2. 若 MemAvailable > 阈值 → 退出
  3. 收集"距当前视口 > 10000 行"的连续区间，长度 >= 1 MiB
  4. madvise(ptr, len, MADV_DONTNEED)
  5. 记录到 unloadedRanges_（std::set<Range>）
  6. TextBuffer::ensureLoaded(range) —— 访问前必须调用；命中 unloadedRanges_ 时 madvise(MADV_WILLNEED)

回退：任何渲染路径访问 unloadedRanges_ 覆盖的偏移时，必须先 ensureLoaded。
      LogViewDelegate::paint 在 paintEngine 之外调用，避免与绘制交错。
```

**已知限制**：`MADV_DONTNEED` 后的再次访问会触发缺页，滚动回该区域时出现可见卡顿。
因此 v1.0 不默认启用，且 `ensureLoaded` 的调用点必须在 `LogViewDelegate::paint` **之前**（由
`LogView::viewportUpdate` 统一调度，见 §7.4）。

### 2.3 `EncodingDetector`

#### 2.3.1 接口

```cpp
// src/core/buffer/EncodingDetector.h
#pragma once
#include "EncodingInfo.h"

namespace tat {

// 纯函数，无状态，可并发调用。
// buffer 只需文件头部样本；recommendSampleSize 给出建议采样长度。
class EncodingDetector {
public:
    // 完整四级检测（L1 BOM → L2 UTF-8 → L3 iconv → L4 ICU → L5 locale）。
    // 仅在 UI 层"编码疑似错误"时使用；索引构建 MUST 用 detectFast（R-04）。
    static EncodingInfo detect(const char* buffer, size_t len);
    static EncodingInfo detect(const char* buffer, size_t len, size_t sampleLimit);

    // 快速路径：仅 L1+L2（BOM + 严格 UTF-8 校验），O(sample)，SIMD 友好。
    // 索引构建阶段使用，避免 GB18030 大文件上的 iconv 试转开销。
    static EncodingInfo detectFast(const char* buffer, size_t len);

    static size_t recommendSampleSize(size_t fileSize);   // clamp(64 KiB, fileSize, 4 MiB)

    // 将任意编码的 UTF-8 字节流转换为目标编码（渲染时使用，§7.3）
    // 返回码：0 成功；-1 非法序列（替换为 U+FFFD 后继续）；-2 内存不足
    static int convert(const char* input, size_t inLen, Encoding from, Encoding to,
                       std::string* out, bool* replaced);

private:
    static EncodingInfo tryBom(const char* b, size_t len);
    static EncodingInfo tryStrictUtf8(const char* b, size_t len);
    static EncodingInfo tryIconv(const char* b, size_t len);
    static EncodingInfo tryIcu(const char* b, size_t len);
    static EncodingInfo tryLocal8Bit(const char* b, size_t len);
};

} // namespace tat
```

#### 2.3.2 四级检测流程

```
detect(buffer, len):
  sample = min(len, recommendSampleSize(len))

  // L1: BOM
  L1 = tryBom(buffer, sample)
  if L1.encoding != Unknown → return L1   // 直接采信，confidence = 1.0

  // L2: UTF-8 严格校验（O(n)，SIMD 友好）
  L2 = tryStrictUtf8(buffer, sample)
  if L2.encoding != Unknown → return L2   // confidence = 1.0 - invalidRatio * 0.5

  // L3: iconv 试转（GB18030 / GBK / CP936 / CP932 / CP950 / CP1252）
  L3 = tryIconv(buffer, sample)
  if L3.encoding != Unknown → return L3

  // L4: ICU ucnv_toUnicode 兜底
  L4 = tryIcu(buffer, sample)
  if L4.encoding != Unknown → return L4

  // 兜底
  return tryLocal8Bit(buffer, sample)     // confidence = 0.3
```

**L2 严格 UTF-8 校验实现要点**（O(n) 单趟）：

```
for each byte b:
  if b < 0x80:            continue                    // ASCII
  if b in [0xC2,0xDF]:   need = 1
  elif b in [0xE0,0xEF]:  need = 2
  elif b in [0xF0,0xF4]:  need = 3
  else:                   invalid++, skip             // 非法首字节或 0x80-0xC1
  校验后续 need 个字节均 in [0x80,0xBF] 且：
    - 0xE0 后首续字节 >= 0xA0（防过短编码）
    - 0xED 后首续字节 <  0xA0（防 surrogate）
    - 0xF0 后首续字节 >= 0x90；0xF4 后首续字节 <= 0x8F
  任一不满足 → invalid++，从头继续
```

**L3 iconv 候选集与顺序**（MUST，顺序敏感）：

```
候选：GB18030 → GBK → CP936 → CP932 → CP950 → ISO-8859-1
规则：iconv(open(候选, "UTF-8")) 试转 sample；
      - 无 EILSEQ 且无 E2BIG → 记为该候选，score = 1.0
      - 有 EILSEQ → 记 invalidRatio，score = max(0, 1.0 - invalidRatio * 4)
      - 选 score 最高者；并列时按候选顺序取前者
```

> **决策**：GB18030 优先于 GBK。GB18030 是 GBK 的超集，且是 Ubuntu 上 `iconv -f` 的规范名。
> ARCH-UB §3.3 列的是 GB18030，此处明确顺序以避免 GBK 误吞 GB18030 的四字节序列。

**L4 ICU**：仅在 `libicu-dev` 可用时链接（CMake 探测 `ICU::UC`），不可用时跳过 L4 直接兜底 L5。

**L5 local8Bit**：`getenv("LC_ALL") ?: getenv("LANG")` 解析出 codeset，映射到 `Encoding` 枚举；
解析失败退化为 `Encoding::Local8Bit`，渲染时用 `QTextCodec::codecForLocale()`。
> **注意**：Qt 6 中 `QByteArray::fromLocal8Bit` 已标记弃用，必须改用 `QTextCodec`，
> 这与 BLUEPRINT §2.3 的写法不同，本文档以本条为准。

#### 2.3.3 采样大小

```
recommendSampleSize(fileSize):
  if fileSize <= 64 KiB  return fileSize
  if fileSize >= 4 MiB   return 4 MiB
  return fileSize        // 中小文件全量检测，避免小文件被截断漏检
```

> 理由：编码错误集中在文件头部（BOM、声明、日志模板）。4 MiB 足以覆盖所有实际场景的头部特征，
> 同时避免在 1 GiB 文件上做全量 UTF-8 校验（会额外消耗 200–400 ms，击穿 §13 的 500 ms 预算）。
> 因此**索引构建阶段**只跑 L1+L2（O(sample)，SIMD 快）；L3/L4 只在 UI 层提示"编码疑似错误"时使用。

### 2.4 `LineIndexer`

#### 2.4.1 接口

```cpp
// src/core/buffer/LineIndexer.h
#pragma once
#include <atomic>
#include <span>
#include <vector>
#include "LineMeta.h"
#include "EncodingInfo.h"
#include "Error.h"

namespace tat {

struct IndexStats {
    int        rowCount      = 0;
    size_t     maxLength     = 0;      // 最长行的字节数
    LineEnd    dominantEnd   = LineEnd::LF;
    uint64_t   totalBytes    = 0;
    double     elapsedMs     = 0;
    bool       hitOffsetLimit = false;  // 触发 > 4 GiB 降级
};

class LineIndexer {
public:
    // 单次调用。data 必须覆盖 bomLen 之后的全文。
    static Error build(const char* data, size_t size, const EncodingInfo& enc,
                       std::vector<LineMeta>* out, IndexStats* stats,
                       const TokenSnapshot* tok, Progress* progress, double* progressPercent);

    // 预估行数（用于进度条），基于前 1 MiB 的换行符密度。
    static int estimateRows(const char* data, size_t size);

private:
    static void scanChunk(const char* p, size_t n, std::vector<LineMeta>* out,
                          std::atomic<size_t>* cursor, IndexStats* stats);
};

} // namespace tat
```

#### 2.4.2 索引算法

```
build(data, size, enc, out, stats, tok, progress):
  1. p  = data + enc.bomLen
  2. n  = size - enc.bomLen
  3. base = p                                    // 偏移以"跳过 BOM 后"为基准
  4. est = estimateRows(data, size)
     out->reserve(min(est, n / 40))              // 40 = 保守的行均长度估计
  5. lineStart = base; curLen = 0
  6. for i in [0, n):
       b = p[i]
       if b == '\n':
           if i > 0 && p[i-1] == '\r':           // \r\n
               push LineMeta{Offset(lineStart - base), Length(curLen - 1)}
               stats.dominantEnd = CRLF
           else:                                  // \n
               push LineMeta{Offset(lineStart - base), Length(curLen)}
               stats.dominantEnd = LF
           curLen = 0; lineStart = p + i + 1
       elif b == '\r':
           if i + 1 < n && p[i+1] == '\n':
               continue                           // 属于 \r\n，由 '\n' 分支处理
           push LineMeta{Offset(lineStart - base), Length(curLen)}
           stats.dominantEnd = CR
           curLen = 0; lineStart = p + i + 1
       else:
           curLen++
       // 进度与取消
       if (i % 65536) == 0:
           if tok && !tok->valid() → return ErrCode::Cancelled
           if progress: progress->current = i; progress->total = n
                        progress->percent = i * 100.0 / n
  7. // 文件末尾无换行
  if curLen > 0 || out->empty():
       push LineMeta{Offset(lineStart - base), Length(curLen)}
     // 注意：curLen == 0 且 out 非空 → 文件以换行结尾，不追加空行
     // 例外：out 为空（整个文件只有行结束符，如 "\n"）→ 追加一个 length=0 的空行，
     //     使 rowCount()==1。空文件（EOF 即文件开头）不进入本流程，rowCount()==0（R-06）
  8. 若任意 LineMeta.offset > UINT32_MAX - 4096 → stats.hitOffsetLimit = true
     并返回 ErrCode::UnsupportedFormat（调用方按 §2.4.4 走分段）
```

**行尾识别顺序（MUST，与 ARCH-UB §3.2 一致）**：`\r\n` → `\n` → `\r`。

> **实现细节**：上面的循环用"看到 `\r` 时先判断下一字节"的方式，避免 `\r\n` 被拆成两行。
> 这是单趟 O(n)，不产生回退。若用正则或 `strstr` 扫描换行符反而更慢（SIMD 的 `memchr` 更快）。

**优化建议（SHOULD）**：内层循环用 `memchr(p + i, '\r', n - i)` 与 `memchr(p + i, '\n', n - i)`
两个跳转扫描替代逐字节循环：

```
loop:
  crPos = memchr(p + i, '\r', n - i)
  lfPos = memchr(p + i, '\n', n - i)
  next  = min(crPos, lfPos)  // 都为空 → 文件末尾
  // 处理 [i, next) 之间的字节；判断 next 是 \r 还是 \n
```

`memchr` 在 glibc 2.35+（Ubuntu 22.04）上是 AVX2/AVX-512 的向量化实现，
P2 场景下比逐字节循环快 6–12×，是达到"索引 1 亿行 < 2 s"目标的关键。

#### 2.4.3 行数预估

```
estimateRows(data, size):
  sample = min(size, 1 MiB)
  count = 扫描 sample 中的 '\n' 数量
  if count == 0 → return 1
  return (size * count) / sample     // 整数除法，向上取整
```

> 用途：仅用于 `QProgressBar` 的分母与 `out->reserve` 的首次容量。
> 不作为精确值，索引完成后以 `out->size()` 为准（进度条此时跳到 100%）。

#### 2.4.4 超大文件降级（> 4 GiB）

`Offset` 为 `uint32_t`，上限约 4 GiB。超过时：

```
1. 若 fileSize <= 4 GiB - 64 KiB → 正常模式
2. 若 fileSize > 4 GiB → 调用 LineIndexer::buildSegmented()
   分段大小 = 1 GiB；每段独立 mmap + 独立索引
   TextBuffer 变成 SegmentedTextBuffer（vector<SegmentMeta> + 每段的 vector<LineMeta>）
   渲染路径不变：LogViewDelegate 用 (rowIndex) 定位到段内偏移
3. 若 fileSize > 32 GiB → 提示用户"文件过大，请使用 --stream 模式（未实现）"
```

> **已知限制**：`SegmentedTextBuffer` 在 v1.0 只实现到接口层面，不做完整实现。
> 理由：日志分析的实际场景极少超过 4 GiB 单文件（超过通常意味着应该先轮转）。
> 但保留接口与降级分支，避免"文件一开就崩"。

### 2.5 `TextBuffer`

#### 2.5.1 接口

```cpp
// src/core/buffer/TextBuffer.h
#pragma once
#include <atomic>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>
#include "EncodingInfo.h"
#include "LineMeta.h"
#include "common.h"
#include "Error.h"

namespace tat {

class MemoryMappedFile;   // 前向声明，避免头文件耦合

// 只读快照。构造完成后内部数据不再变更。
// 发布方式：SharedSnapshot<const TextBuffer> 原子替换（R-01，见 §5.5）。
class TextBuffer {
public:
    TextBuffer() = default;

    // 工厂（R-05）：成功返回非空 shared_ptr；失败返回 nullptr 并填充 *err。
    // 空文件也是合法结果（isValid()==true，rowCount()==0）。
    static std::shared_ptr<TextBuffer> create(const std::string& path, Error* err,
                                              EncodingInfo* enc,
                                              const TokenSnapshot* tok, Progress* progress);

    // 查询（无锁，可并发）
    bool      isValid()              const noexcept { return !m_lines_.empty() || m_size_ > 0; }
    int       rowCount()             const noexcept { return static_cast<int>(m_lines_.size()); }
    size_t    totalBytes()           const noexcept { return m_size_; }
    const std::string& path()        const noexcept { return m_path; }
    const EncodingInfo& encoding()   const noexcept { return m_encoding; }
    const LineMeta* meta(int i)      const noexcept;   // i 越界 → nullptr
    const LineMeta* metaSafe(int i)  const noexcept;   // 同上，noexcept
    std::string_view textAt(int i)   const noexcept;   // 直接指向 mmap，零拷贝
    std::string      toUtf8(int i)   const;             // 需要转码时调用（§7.3）
    const char* basePtr()            const noexcept { return m_base; }
    uint64_t  mtimeNs()              const noexcept { return m_mtimeNs; }

    // 实验特性：受控卸载（默认禁用）
    bool ensureLoaded(size_t offset, size_t length) noexcept;

    // 内存统计（供状态栏与 QA）
    struct MemoryFootprint {
        size_t mapped      = 0;    // mmap 大小（≈文件大小）
        size_t indexMeta   = 0;    // LineMeta 数组
        size_t resident    = 0;    // /proc/self/smaps_rollup 的 Rss
    };
    MemoryFootprint footprint() const;

private:
    std::vector<LineMeta>        m_lines;
    const char*                  m_base   = nullptr;
    size_t                       m_size   = 0;
    std::string                  m_path;
    EncodingInfo                 m_encoding;
    uint64_t                     m_mtimeNs = 0;
    std::shared_ptr<MemoryMappedFile> m_map;   // 保活 mmap
};

} // namespace tat
```

#### 2.5.2 构造流程

```
TextBuffer::create(path, err, enc, tok, progress):
  1. Error e1 = MemoryMappedFile::open(path, ReadOnly, &e)
     e1.hasError() → *err = e1; return nullptr
  2. mem = move(map);  base = mem.data(); size = mem.size()
  3. if size == 0:
       m_lines = {}; m_size = 0; m_map = mem
       return this                       // 空文件：rowCount()==0（R-06）
  4. *enc = EncodingDetector::detectFast(base, min(size, recommendSampleSize(size)))
     // 索引构建阶段只跑 L1+L2（R-04，§2.3.3）
  5. Error e2 = LineIndexer::build(base + enc->bomLen, size - enc->bomLen,
                                    *enc, &m_lines, &stats, tok, progress)
     e2.hasError() → *err = e2; return nullptr
  6. m_base = base;  m_size = size;  m_map = move(mem);  m_mtimeNs = mem.mtimeNs()
  7. return this
```

#### 2.5.3 `textAt` 与 `toUtf8`

```cpp
// textAt：零拷贝，直接返回 mmap 内的 string_view。
// 注意：返回的 string_view 指向 mmap，调用方不得在 TextBuffer 析构后持有。
inline std::string_view TextBuffer::textAt(int i) const noexcept {
    if (i < 0 || i >= static_cast<int>(m_lines.size())) return {};
    const LineMeta& lm = m_lines[i];
    return {m_base + lm.offset, lm.length};
}

// toUtf8：仅当 encoding 不是 UTF-8 家族时做转码。
// 转码结果按行缓存，避免重复转换（缓存 key = 行号，见 §7.3.3；LineMeta 不含 hash，R-07）。
std::string TextBuffer::toUtf8(int i) const {
    auto sv = textAt(i);
    if (m_encoding.isUtf8ByteStream()) return std::string(sv.data(), sv.size());
    bool replaced = false;
    std::string out;
    EncodingDetector::convert(sv.data(), sv.size(), m_encoding.encoding,
                              Encoding::Utf8, &out, &replaced);
    return out;
}
```

> **性能注记**：`toUtf8` 每行分配一次 `std::string`，是渲染路径的开销大头。
> 因此 `LogViewDelegate` **不** 直接调用 `toUtf8`，而是走 §7.3 的"按需转码 + LRU 缓存"策略：
> 视口内 ≤ 100 行，LRU 容量 4096 条，命中时零分配。

#### 2.5.4 `MemoryFootprint`

```
footprint():
  mapped    = m_size
  indexMeta = m_lines.size() * sizeof(LineMeta)
  resident  = 读 /proc/self/smaps_rollup 的 "Rss:" 行，单位 kB → bytes
  return {mapped, indexMeta, resident}
```

> `resident` 反映**物理内存**占用，与 `mapped`（虚拟地址空间）不同。
> mmap 的页只在被访问时才计入 Rss，因此 P1 场景下 `resident` 通常远小于 `mapped`。
> 状态栏展示 `resident`，而非 `mapped`，避免误导用户。

### 2.6 `FileWatcher`

#### 2.6.1 接口

```cpp
// src/core/buffer/FileWatcher.h
#pragma once
#include <QObject>
#include <QString>
#include <cstdint>

namespace tat {

// 单文件监听。fd 数量受 max_user_watches 限制，因此每次仅监听当前文档。
class FileWatcher : public QObject {
    Q_OBJECT
public:
    explicit FileWatcher(QObject* parent = nullptr);
    ~FileWatcher();

    bool watch(const QString& path);        // 失败返回 false 并 qWarning
    void unwatch();
    bool isWatching() const noexcept;

signals:
    void externalChanged(const QString& path);   // 主线程投递
    void watchFailed(const QString& path);

private:
    int      m_fd = -1;
    int      m_wd = -1;
    QString  m_path;
};

} // namespace tat
```

#### 2.6.2 实现要点

```
watch(path):
  1. unwatch()                     // 先清理旧的
  2. m_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC)
     < 0 → return false
  3. m_wd = inotify_add_watch(m_fd, path.toUtf8(),
                              IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB)
     < 0 → close(m_fd); return false
  4. 安装 QSocketNotifier(m_fd, Read) → 在可读时读事件，emit externalChanged

事件处理：
  read(m_fd, buf, 4096) 循环直到 EAGAIN
  解析 inotify_event 链表；忽略 IN_ISDIR
  emit externalChanged(m_path)      // 主线程，通过 QSocketNotifier 已保证
```

**MUST**：

- 触发后**不自动重载**，仅在状态栏显示"文件已被外部修改，点击重载"。
  > 理由：用户可能正在编辑规则，自动重载会丢失当前滚动位置与选中状态。
- 每次仅监听 1 个文件。多文件场景（如打开多个标签）v1.0 不支持。
- `IN_ATTRIB` 覆盖 `chmod`/`chmod`/权限变更，避免用户改权限后看不到提示。
- 文件被 `mv` 走（`IN_MOVED_FROM`）时，emit 后 `unwatch()`（原路径失效）。

**已知限制**：

- 通过 NFS 或网络文件系统打开的文件，`inotify` 不可靠（内核层面），
  此时 `watch()` 返回 true 但事件可能延迟数秒。状态栏追加"网络文件，变更检测可能延迟"。
- 监控 fd 计入进程的 `RLIMIT_NOFILE`，与 `MemoryMappedFile` 关闭 fd 的设计配合。

### 2.7 DAL 并发与错误对照

| 操作 | 线程 | 锁 | 失败码 | 恢复策略 |
| :--- | :--- | :--- | :--- | :--- |
| `MemoryMappedFile::open` | W | 无 | FileNotFound / PermissionDenied / MapFailed | 弹窗 + 保留旧文件 |
| `EncodingDetector::detect` | W | 无 | 无（兜底 Local8Bit） | — |
| `LineIndexer::build` | W | 无 | Cancelled / UnsupportedFormat | 取消 → 提示；超大 → 分段 |
| `TextBuffer::create` | W | 无 | 透传 | 主线程 resetModel |
| `FileWatcher::watch` | M | 无 | 内部 qWarning | 状态栏提示 |
| `TextBuffer::textAt` | M/W | 无 | 越界返回空 | 调用方判空 |
| `TextBuffer::toUtf8` | M | 无 | 内存不足 → 空串 | 渲染降级为 Latin-1 |

---

## 3. 业务逻辑层（BLL）

### 3.1 设计原则

1. **零 Qt 依赖**：`core/engine/*.h|cpp` 不得包含任何 `<Q*>` 头文件。颜色用 `Argb` 传入，
   正则编译通过注入的 `CompileFn` 回调完成（§4.1）。
2. **纯函数 + 显式上下文**：`FilterEngine`、`Searcher`、`MarkerManager` 不持有 UI 指针，
   所有副作用通过返回值或出参表达。
3. **可独立单测**：BLL 不需要 `QApplication` 即可测试，`tst_filter.cpp` 直接调用静态方法。
4. **不可变快照**：规则集在任务期间不可变，通过 `shared_ptr<const RuleSet>` 发布。

### 3.2 `FilterEngine`

#### 3.2.1 接口

```cpp
// src/core/engine/FilterEngine.h
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "FilterRule.h"
#include "RowState.h"
#include "Task.h"
#include "TextBuffer.h"

namespace tat {

// 每行最终结果 + 颜色引用。1 字节/行。
// 颜色不在 Buffer 中存放，按 ruleRef 查 RuleSet（§3.3）。
struct FilterResult {
    std::vector<uint8_t> states;   // size == rowCount
    int     ruleCount = 0;
    uint64_t ruleFingerprint = 0;
    int     generation = 0;
    std::array<int, kMaxRules> matchCounts{};   // 按 rule id 索引（R-03：上限 kMaxRules=64）
};

// 正则编译回调。BLL 不依赖 Qt，编译逻辑由 UI 层注入（§4.1）。
using CompileFn = std::function<int(const std::string& pattern, bool caseSensitive,
                                    bool wholeWord, std::string* outCompiled,
                                    std::string* err)>;

// 匹配回调。同样是注入的：Substring 走 std::search，Regex 走 PCRE2。
using MatchFn = std::function<bool(const std::string_view line, const std::string& compiled,
                                   const std::string& pattern, bool caseSensitive)>;

class FilterEngine {
public:
    // 同步执行（用于单测与 --export 命令行）。
    static Error apply(const TextBuffer& buffer, const RuleSet& rules,
                       FilterResult* out, const TokenSnapshot* tok, Progress* progress);

    // 分片并发执行（生产路径）。workerCount == 0 时使用 QThread::idealThreadCount()。
    // 该函数不阻塞调用方：内部提交到外部传入的 threadPool 抽象。
    static Error applyParallel(const TextBuffer& buffer, const RuleSet& rules,
                               int workerCount, FilterResult* out,
                               const TokenSnapshot* tok, Progress* progress,
                               const CompileFn& compile, const MatchFn& match,
                               ThreadPool* pool);

    // 单行判定（用于 UI 高亮当前行，非全量）。
    static ResultState classifyLine(std::string_view line, const RuleSet& rules,
                                    const MatchFn& match, int* ruleRef);

    // 规则集指纹（用于缓存失效判断）。
    static uint64_t fingerprint(const RuleSet& rules);

    // 从一行文本生成规则（双击行 → "以此行为过滤条件"）。
    static FilterRule ruleFromLine(const std::string& line, FilterAction action, MatchMode mode);
};

} // namespace tat
```

#### 3.2.2 状态决策（R-19：列表顺序语义）

规则按【RuleSet 中的顺序】求值，**第一条命中的规则决定行命运**——
与原版 TAT 的多规则行为一致：

```
classifyLine(line, rules, match, ruleRef):
  if rules.anyRule == false → return Normal, 0
  for i in [0, rules.rules.size()):
      if ruleHit(line, rules.rules[i], i, rules, match):
          ruleRef = i
          return rules.rules[i].action == Exclude ? Hidden : Highlighted
  return Normal, 0
```

| 场景 | 规则列表（按顺序） | 行内容 | 结果 | 说明 |
| :--- | :--- | :--- | :--- | :--- |
| 1 | ∅ | 任意 | **Normal** | 无规则 |
| 2 | [Exclude "error"] | "error 42" | **Hidden** | Excluding 命中 → 隐藏 |
| 3 | [Include "error"] | "error 42" | **Highlighted** | 着色规则命中 → 按 Text/Background 着色 |
| 4 | [Include "error"] | "info" | **Normal** | 未命中 → 正常显示（R-19：不再 Dimmed） |
| 5 | [Include "keep", Exclude "skip"] | "skip me" | **Hidden** | 顺序：keep 未命中 → skip 命中 |
| 6 | [Exclude "skip", Include "keep"] | "keep me" | **Highlighted** | 顺序：skip 未命中 → keep 命中 |
| 7 | [Exclude "keep", Include "keep"] | "keep me" | **Hidden** | 首条（Exclude）命中即决定 |

**历史注记**：v1.0/v1.1 的"include 两轮优先 + Dimmed 弱化"决策表废止。
Dimmed 枚举值保留（RowState 兼容），但引擎不再产生该状态。

#### 3.2.3 分片与并发

```
applyParallel(buffer, rules, workerCount, out, tok, progress, compile, match, pool):
  1. N = buffer.rowCount()
  2. out->states.resize(N)
     out->ruleCount = rules.rules.size()
     out->ruleFingerprint = rules.fingerprint
     out->generation = rules.generation
     fill(out->matchCounts, 0)
  3. W = workerCount > 0 ? workerCount : idealThreadCount()
     chunkSize = max(1024, N / (W * 8))
     // ARCH-UB §4.1 原值。W*8 的目的是让任务数 >= 线程数的 8 倍，
     // 避免尾部线程空转。
  4. jobs = ceil(N / chunkSize)
  5. atomic<size_t> doneLines(0)
     atomic<size_t> doneBytes(0)
  6. 提交 jobs 个任务到 pool：
       task(j):
         start = j * chunkSize
         end   = min(start + chunkSize, N)
         // 每 chunk 处理前检查取消
         if tok && !tok->valid() → return
         // 每个 chunk 内每 8KB 再检查一次（§3.2.4）
         localHits = 0
         for i in [start, end):
           sv = buffer.textAt(i)
           state, ref = classifyLine(sv, rules, match, ...)
           out->states[i] = RowState::make(state, ref)
           if state == Highlighted or state == Dimmed:
             localHits++
             // matchCounts 需要原子累加，见下
         doneLines.fetch_add(end - start, relaxed)
         doneBytes.fetch_add(sumByteLen(start, end), relaxed)
         if progress: 更新 current/total/percent
  7. 等待所有任务完成（pool.waitForDone 带超时，见 §3.2.5）
  8. 返回 Ok
```

**`matchCounts` 的原子累加**：每个 worker 在本地累加 `std::array<int, kMaxRules> localCounts`，
任务结束后通过 `fetch_add` 合并到 `out->matchCounts`。避免每行一次原子操作。

**分片大小推导**：

| 场景 | N | W | chunkSize | jobs |
| :--- | ---: | ---: | ---: | ---: |
| P2（1 亿行） | 1e8 | 8 | 156 250 | 641 |
| P1（3000 万行） | 3e7 | 8 | 46 875 | 640 |
| 小文件（1 万行） | 1e4 | 8 | 1024 | 10 |

> 小文件时 chunkSize 被 clamp 到 1024，jobs 最少 10，保证多核利用。

#### 3.2.4 取消协议（细化）

```
用户修改规则时（MainController::applyFilter）：
  1. token->cancel()                // gen++，旧任务观察到后自行退出
  2. pool 中仍在运行的任务：
     - 每个 chunk 内每 8 KB 检查 tok->valid()
     - 检查失败 → 立即 return，不提交剩余结果
     - out->states 已写入的部分保留（渲染时可显示"部分结果"）
  3. 新任务提交：
     - 用新 Token（gen = old + 1）
     - pool.waitForDone(500) —— 只等 500 ms，超时则放弃等待
     - 若 500 ms 内旧任务未完成，继续提交新任务
       旧任务在后台继续跑完但结果被丢弃（out 已被新任务覆盖）
```

> **决策**：BLUEPRINT §3.1 建议"取消旧任务，提交新任务"，但未说明等待策略。
> v1.0 选择**不阻塞主线程**：`waitForDone(500)` 超时后立即提交新任务，
> 旧任务在后台跑完但结果被 `generation` 判过期丢弃。
> 理由：正则回溯可能跑几分钟（§3.2.6 有超时保护），阻塞主线程会冻结 UI。
> 代价是短暂的双倍 CPU 占用（旧任务 + 新任务），但 `nice(19)` 已限制其优先级。

**取消检查的粒度**：每 8 KB 检查一次，而非每行。理由：

- 每行检查一次原子 load 在 P2 场景（1 亿行）下增加约 200 ms 开销。
- 8 KB 粒度对应约 1000–2000 行，最坏情况下取消延迟约 50–200 ms，用户可接受。
- 检查用 `tok->valid()` 而非 `load(acquire)`，因为 `gen_` 是 `relaxed` 语义即可满足需求。

#### 3.2.5 进度回报

```
进度粒度：每 10% 触发一次主线程回调（ARCH-UB §8）。
实现：worker 线程维护 atomic<size_t> doneLines，
     每次 fetch_add 后若 (doneLines / N * 10) 跨过整数边界 → 通过
     QMetaObject::invokeMethod(mainController, "onProgress", Qt::QueuedConnection, progress)
```

> **注意**：BLL 层不直接调用 `QMetaObject`。进度回调通过注入的 `std::function<void(const Progress&)> onProgress` 参数传入，
> 由 UI 层负责投递到主线程。这样 BLL 保持零 Qt 依赖。

#### 3.2.6 正则超时保护

```
applyParallel 中每个 chunk 的任务开始时记录 chunkStartMs。
每个 chunk 内每 8 KB 检查：
  if now() - chunkStartMs > 3000 ms:
      emit onTimeout(ruleId, pattern)   // 注入的回调
      return ErrCode::Timeout
```

**UI 层响应**（§8.3）：

- 第一个 chunk 超时 → `QMessageBox::question` 弹窗："正则可能过慢，是否停止？"
- 用户点"停止" → `token->cancel()`，`FilterEngine` 返回 `ErrCode::Cancelled`
- 用户点"继续" → 忽略，继续跑（最多再跑 3 s 后再次弹窗）

> **已知限制**：PCRE2 的回溯深度无法在应用层限制（需要 JVM 式的 -Xss）。
> 本设计只限制**总耗时**，不限制单条正则的回溯步数。
> 若单条正则在某一行上卡死（如 `(a+)+` 配 100KB 行），3 s 超时才能触发，
> 此时 UI 无响应。缓解：在 §7.3 的渲染路径上对单行长度设上限（> 1 MB 截断显示）。

### 3.3 颜色映射与 `ruleColorRef`

#### 3.3.1 设计

BLUEPRINT 把颜色存在 `FilterRule` 上，渲染时查表；ARCH-UB 未细化。
若把颜色**复制到每行**（`std::array<Argb, N>`），P2 场景需 400 MB，击穿预算。

**决策**：`FilterResult.states[i]` 只存 `ruleRef`（6 bit），颜色按 ruleRef 查 `RuleSet.rules`：

```cpp
// LogViewDelegate::resolveColors(rowState, rules)
Argb fg, bg;
switch (rowState.state()) {
case ResultState::Highlighted: {
    int ref = rowState.ruleRef();
    if (ref < rules.rules.size()) {
        const FilterRule& r = rules.rules[ref];
        fg = r.foreground; bg = r.background;
    } else { fg = 0xFF000000; bg = 0xFFFFFFF0; }   // 兜底黄色
    break;
  }
case ResultState::Dimmed:
    fg = 0xFF808080; bg = 0x00000000;   // 灰色前景，透明背景
    break;
case ResultState::Hidden:
    fg = bg = 0;   // 不绘制
    break;
case ResultState::Normal:
    fg = palette.foreground(); bg = palette.background();
    break;
}
```

#### 3.3.2 内存账（P2 场景，1 亿行）

| 项 | 大小 | 说明 |
| :--- | ---: | :--- |
| `states` | 100 MB | 1 B/行 |
| `matchCounts` | 256 B | 64 × int（kMaxRules，R-03） |
| 规则集 | < 1 MB | 最多 64 条（kMaxRules） |
| **合计** | **≈ 100 MB** | 对比"每行存 4 字节颜色"方案的 500 MB |

**结论**：`ruleColorRef` 方案节省 80% 结果内存。

#### 3.3.3 `ruleRef` 索引规则

- `ruleRef` 是 `RuleSet.rules` 数组的**索引**（0-based），不是 `FilterRule.id`。
  理由：`RuleSet.rules` 已过滤 `isEnabled`，索引连续且无空洞。
- 若用户在 Apply 完成后修改规则（如禁用某条），`ruleRef` 可能指向错误位置。
  缓解：`FilterResult.generation` 与 `RuleSet.generation` 必须匹配，
  `LogViewDelegate::paint` 检查 `result.generation == rules.generation`，不匹配则按 `Normal` 渲染。

### 3.4 `MarkerManager`

#### 3.4.1 决策与理由

BLUEPRINT §3.2 与 ARCH-UB §4.2 都指定 `std::unordered_set<int>`，
理由是"添加/删除 O(1)"与"1 亿行内标记跳转 < 1 ms"。

**问题**：`unordered_set` 的迭代顺序**不保证有序**，`std::next(set.find(current))`
得到的是哈希桶的下一个元素，**不是"下一个更大的行号"**。这与"循环跳转"的语义不符。

**决策（§0.4 #4）**：改用 `std::array<std::vector<int>, kMarkerCount>`，每个 vector 按行号严格升序。

| 操作 | `unordered_set` | `vector`（本文档） |
| :--- | :--- | :--- |
| 添加 | O(1) 均摊 | O(log n) + O(k) 移动 |
| 删除 | O(1) 均摊 | O(log n) + O(k) 移动 |
| 查找 | O(1) 均摊 | O(log n) |
| 下一个更大元素 | **❌ 无序** | O(1)（找到后 `find` 迭代器 + 1） |
| 内存 | 高（每节点 32–48 B + 桶数组） | 低（连续，每元素 4 B） |
| 1 亿行 100 万标记 | ~40 MB | ~4 MB |

**结论**：`vector` 在所有目标操作上都满足或超过 ARCH-UB 的 < 1 ms 要求，
且修复了"下一个更大元素"的语义缺陷。

#### 3.4.2 接口

```cpp
// src/core/engine/MarkerManager.h
#pragma once
#include <array>
#include <algorithm>
#include <optional>
#include <vector>
#include "Marker.h"
#include "common.h"

namespace tat {

// 线程安全：MarkerManager 在主线程独占（MUST）。
// 标记跳转是 O(log n)，在 GUI 主线程上 < 1 ms，无需移到后台。
class MarkerManager {
public:
    explicit MarkerManager(int capacity = 0);

    // 切换当前行的 markerId（1..8）。若已存在则移除。
    // 返回 true 表示"添加"，false 表示"移除"。
    bool toggle(int line, int markerId);

    // 显式添加/删除
    bool add(int line, int markerId);
    bool remove(int line, int markerId);
    bool has(int line, int markerId) const noexcept;
    int  markerOf(int line) const noexcept;   // 返回 1..8 或 0（无）

    // 循环跳转：从 line 之后找下一个同 markerId 的行
    // forward=true 找更大行号，false 找更小；到头后环绕。
    std::optional<int> next(int line, int markerId, bool forward = true) const;

    // 统计
    int  count(int markerId) const noexcept;
    int  totalCount() const noexcept;
    std::array<std::vector<int>, kMarkerCount> dump() const;

    // 序列化（.tat 中不包含标记，标记是运行时状态；此处用于会话恢复）
    void save(std::vector<Marker>* out) const;
    void load(const std::vector<Marker>& markers);

    void clear() noexcept;

private:
    std::array<std::vector<int>, kMarkerCount> m_markers{};
};

} // namespace tat
```

#### 3.4.3 关键算法

```cpp
// 添加：二分定位 + insert
bool MarkerManager::add(int line, int markerId) {
    if (markerId < 1 || markerId > kMarkerCount) return false;
    if (line < 1) return false;
    auto& v = m_markers[markerId - 1];
    auto it = std::lower_bound(v.begin(), v.end(), line);
    if (it != v.end() && *it == line) return false;   // 已存在
    v.insert(it, line);                                // O(log n) 查找 + O(k) 移动
    return true;
}

// 切换
bool MarkerManager::toggle(int line, int markerId) {
    if (has(line, markerId)) return !remove(line, markerId);
    return add(line, markerId);
}

// 查找：O(log n)
bool MarkerManager::has(int line, int markerId) const noexcept {
    if (markerId < 1 || markerId > kMarkerCount) return false;
    const auto& v = m_markers[markerId - 1];
    return std::binary_search(v.begin(), v.end(), line);
}

// 当前行的标记（遍历 8 个 vector，O(8 * log n)）
int MarkerManager::markerOf(int line) const noexcept {
    for (int i = 0; i < kMarkerCount; ++i) {
        if (std::binary_search(m_markers[i].begin(), m_markers[i].end(), line))
            return i + 1;
    }
    return 0;
}

// 循环跳转
std::optional<int> MarkerManager::next(int line, int markerId, bool forward) const {
    if (markerId < 1 || markerId > kMarkerCount) return std::nullopt;
    const auto& v = m_markers[markerId - 1];
    if (v.empty()) return std::nullopt;

    if (forward) {
        auto it = std::upper_bound(v.begin(), v.end(), line);   // 第一个 > line
        if (it == v.end()) it = v.begin();                        // 环绕
        return *it;
    } else {
        auto it = std::lower_bound(v.begin(), v.end(), line);    // 第一个 >= line
        if (it == v.begin()) it = v.end();                        // 环绕
        --it;
        return *it;
    }
}
```

#### 3.4.4 复杂度与目标验证

| 操作 | 复杂度 | 1 亿行 / 100 万标记 | 目标 |
| :--- | :--- | :--- | :--- |
| `add` | O(log n) | ~20 次比较 + 4 MB 移动 ≈ 0.02 ms | < 1 ms ✅ |
| `remove` | O(log n) | ~20 次比较 + 4 MB 移动 ≈ 0.02 ms | < 1 ms ✅ |
| `has` | O(log n) | ~20 次比较 ≈ 0.001 ms | < 1 ms ✅ |
| `markerOf` | O(8 log n) | ~160 次比较 ≈ 0.01 ms | < 1 ms ✅ |
| `next` | O(log n) | ~20 次比较 + 1 步迭代 ≈ 0.001 ms | < 1 ms ✅ |

> 注：4 MB 移动在 L3 缓存（典型 32 MB）内，实测约 1–2 GB/s，0.02 ms 是上限估计。
> 实测在 M1/M2 里程碑的 `tst_marker.cpp` 中固化阈值。

#### 3.4.5 会话恢复

标记不写入 `.tat`（`.tat` 只存规则），但写入会话（`QSettings`）：

```
Settings key: session/markers
格式：QStringList，每项 "line:markerId"，如 "12345:3"
容量上限：10000 条；超出时按 markerId 分组后只保存前 1000 条/组
```

> 理由：标记是用户的工作状态，应在下次启动时恢复。但 1 亿行可能有数万标记，
> 全部写入 QSettings 会显著增加启动时间，因此设上限。

### 3.5 `Searcher`

#### 3.5.1 接口

```cpp
// src/core/engine/Searcher.h
#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "Task.h"
#include "TextBuffer.h"

namespace tat {

struct SearchOptions {
    std::string pattern;
    bool        useRegex      = false;
    bool        caseSensitive = false;
    bool        wholeWord     = false;
    int         startLine     = 1;     // 1-based，从该行的下一处继续
};

struct SearchHit {
    int     line = 0;        // 1-based
    size_t  offset = 0;      // 行内字节偏移（非字符偏移，§3.5.4）
    size_t  length = 0;
};

struct SearchResult {
    std::vector<SearchHit> hits;
    int     truncated = 0;        // 超出 maxHits 的数量
    bool    cancelled = false;
    double  elapsedMs = 0;
};

class Searcher {
public:
    using CompileFn = FilterEngine::CompileFn;
    using MatchFn   = FilterEngine::MatchFn;

    // 全量搜索（后台线程）
    static Error search(const TextBuffer& buffer, const SearchOptions& opt,
                        const CompileFn& compile, const MatchFn& match,
                        SearchResult* out, const TokenSnapshot* tok, Progress* progress,
                        int maxHits = 10000);

    // 增量搜索（用户在搜索框输入时，主线程调用，限当前视口 ±500 行）
    static Error searchViewport(const TextBuffer& buffer, const SearchOptions& opt,
                                int anchorLine, int radius,
                                const MatchFn& match,
                                std::vector<SearchHit>* out);
};

} // namespace tat
```

#### 3.5.2 快速路径与正则路径

```
search(buffer, opt, compile, match, out, tok, progress, maxHits):
  1. 判定走哪条路径：
     if opt.useRegex:
         err = compile(opt.pattern, opt.caseSensitive, opt.wholeWord, &compiled, &e)
         err != 0 → return ErrCode::RegexError
         path = Regex
     else:
         needle = opt.pattern
         if !opt.caseSensitive: needleLower = toLower(needle)
         path = Substring

  2. out->hits.reserve(min(maxHits, buffer.rowCount() / 10))
  3. for line in [opt.startLine, buffer.rowCount()]:
       if out->hits.size() >= maxHits → out->truncated = ...; break
       if (line % 10000 == 0) && !tok->valid() → out->cancelled = true; break
       sv = buffer.textAt(line)
       switch path:
         Substring:
           // 快速路径：QByteArray::contains 的 C++ 等价物
           if opt.caseSensitive:
               pos = findCaseSensitive(sv, needle)
           else:
               pos = findCaseInsensitive(sv, needleLower)
           if pos != npos:
               out->hits.push_back({line, pos, needle.size()})
         Regex:
           m = match(sv, compiled, opt.pattern, opt.caseSensitive)
           if m: out->hits.push_back({line, m.offset, m.length})
       progress->current = line; progress->total = buffer.rowCount()
  4. return Ok
```

#### 3.5.3 简单包含的快速实现

BLUEPRINT §3.3 提到"Boyer-Moore-Horspool 或 `QString::indexOf`"。
v1.0 决策：**不使用 BMH**，改用 `memmem` 或自实现的 Two-Way 算法。

```cpp
// Two-Way 算法，O(n + m) 最坏，实际约 2-5 倍快于朴素匹配。
// 比 BMH 更稳定（BMH 在坏数据上退化到 O(n*m)）。
static size_t findTwoWay(const char* haystack, size_t n,
                         const char* needle, size_t m);

// 大小写不敏感版本：先转小写，或逐字节 tolower 比较。
// 对纯 ASCII 场景，tolower 查表 O(1)。
static size_t findTwoWayI(const char* haystack, size_t n,
                          const char* needle, size_t m);
```

> **决策**：不引入外部库（如 Hyperscan、Hund），避免打包复杂度。
> `memmem` 在 glibc 2.26+（Ubuntu 18.04+）有实现，但行为不完全符合 Two-Way，
> 故自实现以保证跨版本一致性。
> 性能目标：ASCII 纯日志场景下比 `std::search` 快 5–10×，与 ARCH-UB §4.3 一致。

#### 3.5.4 `offset` 与字符偏移

`SearchHit.offset` 是**行内字节偏移**，不是字符偏移。

- 渲染高亮时（§7.3），`LogViewDelegate` 用 `QString::fromUtf8(sv)` 后
  再用 `QString` 的 `QTextCursor` 定位到字节偏移对应的字符位置。
- 对 UTF-8 文本，字节偏移 → 字符偏移需要 O(行长) 扫描，故**每行只算一次**，
  结果缓存在 `QHash<LineNo, QVector<int>>` 中（视口内 ≤ 100 行，可接受）。
- 对非 UTF-8（如 GB18030），`offset` 是原始编码的字节偏移，转码后需重新计算。

> **已知限制**：对非 UTF-8 编码，搜索高亮可能错位 1–2 像素。
> v1.0 选择"字节偏移优先"以保持性能；字符偏移支持排入 v1.1。

#### 3.5.5 增量搜索

```
searchViewport(buffer, opt, anchorLine, radius, match, out):
  start = max(1, anchorLine - radius)
  end   = min(buffer.rowCount(), anchorLine + radius)
  复用 search() 的逻辑，但遍历范围限制在 [start, end]
  maxHits = 100
```

**UI 层用法**（§7.6）：

- 用户按 `Ctrl+F` 打开搜索框
- 输入时：`searchViewport` 在当前视口 ±500 行内搜索，结果显示在下拉
- 用户按 `Enter`：触发全量 `search()`（后台线程）
- 全量结果用于"下一个/上一个"跳转

> 增量搜索限制在 1000 行内，避免输入时全量扫描导致 UI 卡顿。

### 3.6 BLL 并发与错误对照

| 操作 | 线程 | 锁 | 失败码 |
| :--- | :--- | :--- | :--- |
| `FilterEngine::apply` | W | 无 | Cancelled / RegexError |
| `FilterEngine::applyParallel` | W（多） | 无（atomic 计数） | Cancelled / Timeout |
| `FilterEngine::classifyLine` | M/W | 无 | 无 |
| `FilterEngine::fingerprint` | M/W | 无 | 无 |
| `MarkerManager::*` | M | 主线程独占 | 参数错误返回 false |
| `Searcher::search` | W | 无 | Cancelled / RegexError |
| `Searcher::searchViewport` | M | 无 | 无 |

---

## 4. 跨层设计决策

本章收录跨层的设计选择，它们不属于单一模块，但约束多个模块的实现方式。

### 4.1 正则编译注入（`CompileFn`）

**问题**：BLUEPRINT §3.1 把 `QRegularExpression compiledRegex` 作为 `FilterRule` 的成员，
导致 BLL 头文件依赖 Qt。这违反 §0.3 硬约束 1。

**决策**：BLL 不直接持有 Qt 正则对象，而是通过 `CompileFn` 回调注入编译能力。
`FilterRule.compiled` 存放编译产物的不透明字节（`std::optional<std::string>`），
实际编译与匹配由 UI 层注入的回调完成。

```cpp
// UI 层注入的编译回调（MainWindow 构造时设置）
CompileFn compileFn = [](const std::string& pattern, bool caseSensitive,
                         bool wholeWord, std::string* out, std::string* err) -> int {
    QRegularExpression re(QString::fromStdString(pattern));
    if (!caseSensitive) re.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    if (wholeWord) re.setPatternOptions(re.patternOptions() | QRegularExpression::UseUnicodeWordBoundary);
    // 空模式显式拒绝（v1.1 修订：Qt 6.4 无 QRegularExpression::DontMatchEmptyString
    // 枚举，等效语义由"pattern 为空时编译失败/匹配恒 false"实现）
    if (re.isValid()) {
        // 将编译产物序列化为不透明字节（Qt 6 的 QRegularExpression 内部用 PCRE2，
        // 此处用 pattern 字符串 + options 作为"缓存键"，实际匹配时重新构造 re）
        // 注意：Qt 6 没有公开的"序列化已编译正则"API，因此 v1.0 的"预编译"
        // 实际是"预验证 + 缓存匹配行为"，真正的预编译在 v1.1 用 PCRE2 直接调用实现。
        *out = pattern;   // 占位：v1.0 用 pattern 本身作为缓存键
        return 0;
    }
    *err = re.errorString().toStdString();
    return -1;
};
```

> **已知限制（v1.0）**：Qt 6 的 `QRegularExpression` 没有公开的序列化 API，
> 因此"预编译"在 v1.0 实际是"预验证 + 预构造"，每个 chunk 任务开始时构造一次 `QRegularExpression`，
> 而非复用编译产物。
> **性能影响**：每个 chunk（约 15 万行）构造一次正则，耗时约 0.5–2 ms，P2 场景总计约 1–3 s。
> 这是 v1.0 的性能债务，v1.1 改用 PCRE2 C API（`pcre2_compile` + `pcre2_code`）实现真正的预编译。
> **缓解**：v1.0 在 `FilterEngine::applyParallel` 中，每个 chunk 任务开始时构造正则，
> 并在 chunk 内复用（约 15 万行共享一个 `QRegularExpression` 对象），将构造开销摊薄到每行 0.01 μs。

#### 4.1.1 匹配回调

```cpp
MatchFn matchFn = [](std::string_view line, const std::string& compiled,
                     const std::string& pattern, bool caseSensitive) -> bool {
    // v1.0 实现：每次调用构造 QRegularExpression（开销约 0.5 ms/15 万行）
    // v1.1 实现：用 pcre2_match 直接调用编译产物
    QRegularExpression re(QString::fromStdString(pattern));
    if (!caseSensitive) re.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    // 空模式显式拒绝（v1.1 修订：Qt 6.4 无 QRegularExpression::DontMatchEmptyString
    // 枚举，等效语义由"pattern 为空时编译失败/匹配恒 false"实现）
    QRegularExpressionMatch m = re.match(QStringView(QString::fromUtf8(line.data(), line.size())));
    return m.hasMatch();
};
```

> **v1.0 的取舍**：为了快速交付，v1.0 接受"每 chunk 构造一次正则"的开销。
> 若性能不达标（P2 场景 > 3 s），v1.1 优先改用 PCRE2 C API。

### 4.2 线程池抽象

BLL 不直接依赖 `QThreadPool`，而是通过 `ThreadPool` 抽象：

```cpp
// src/core/engine/ThreadPool.h
#pragma once
#include <functional>
#include <vector>

namespace tat {

// 线程池抽象。BLL 通过它提交任务，不感知 Qt。
// UI 层用 QThreadPool 实现该接口（见 §5.3）。
class ThreadPool {
public:
    virtual ~ThreadPool() = default;

    // 提交任务，返回任务 ID。maxConcurrent 限制并发数。
    virtual int submit(std::function<void()> task) = 0;

    // 等待所有任务完成。timeoutMs < 0 表示无限等待。
    // 返回 true 表示全部完成，false 表示超时。
    virtual bool waitForDone(int timeoutMs) = 0;

    // 当前活跃任务数
    virtual int activeTaskCount() = 0;
};

// UI 层实现
class QThreadPoolAdapter : public ThreadPool {
    QThreadPool* m_pool;
public:
    explicit QThreadPoolAdapter(QThreadPool* pool);
    int submit(std::function<void()> task) override;
    bool waitForDone(int timeoutMs) override;
    int activeTaskCount() override;
};

} // namespace tat
```

**接口契约**：

- `submit` 必须是非阻塞的：提交后立即返回，任务在后台线程执行。
- `waitForDone` 必须在调用方线程阻塞（用于 §3.2.4 的 500 ms 超时等待）。
- `activeTaskCount` 是近似值，用于状态栏显示。

### 4.3 颜色与规则索引的契约

`FilterResult.states[i]` 中的 `ruleRef` 是 `RuleSet.rules` 数组的**索引**（0-based），
不是 `FilterRule.id`。

**契约（MUST）**：

1. `RuleSet.rules` 在 Apply 提交时构造，此后**不可变**。
2. `FilterResult.generation` 必须等于 `RuleSet.generation`，否则渲染按 `Normal` 处理。
3. `ruleRef` 超出 `rules.size()` 时，按兜底颜色渲染（`§3.3.1` 的黄色）。
4. `matchCounts` 按 `ruleId`（非索引）索引，因为 `ruleId` 在规则删除后不复用。

### 4.4 状态决策表摘要

完整决策表见 §3.2.2。此处只记录跨层契约：

- `ResultState` 是 2 bit 枚举，存储在 `uint8_t` 的低 2 位。
- `ruleRef` 是 6 bit，存储在 `uint8_t` 的高 6 位。
- 渲染层（§7.3）根据 `(state, ruleRef)` 查 `RuleSet.rules` 获取颜色。
- 搜索高亮（下划线）**不**存储在 `FilterResult` 中，由 `Searcher` 独立产出
  （`std::vector<SearchHit>`），渲染时叠加。

### 4.5 标记存储决策

BLUEPRINT §3.2 与 ARCH-UB §4.2 指定 `unordered_set`，但 `unordered_set` 不保证迭代顺序，
无法实现"下一个更大行号"的语义。

**决策**：改用 `std::array<std::vector<int>, 8>`，每个 vector 按行号升序。
完整实现与复杂度分析见 §3.4。

---

## 5. 并发模型与双缓冲

### 5.1 线程角色

```
┌──────────────────────────────────────────────────────────────────────────┐
│ GUI 主线程 [M]                                                            │
│   - 事件循环（Qt event loop）                                             │
│   - UI 渲染（LogViewDelegate::paint）                                     │
│   - MarkerManager 操作（主线程独占）                                       │
│   - 用户输入处理（键盘、鼠标、拖放）                                        │
│   - QSettings 读写                                                       │
│   - FileWatcher 事件接收                                                  │
│                                                                          │
│   禁止：I/O、正则匹配、mmap 系统调用、> 1 ms 的计算                        │
├──────────────────────────────────────────────────────────────────────────┤
│ QThreadPool 工作线程 [W]（线程数 = idealThreadCount()）                   │
│   - LineIndexer::build                                                    │
│   - FilterEngine::applyParallel（分片并发）                                │
│   - Searcher::search                                                      │
│   - TatSerializer::write（大文件时）                                       │
│                                                                          │
│   约束：nice(19)，每 8 KB 检查 Token，不访问 Qt 对象                        │
├──────────────────────────────────────────────────────────────────────────┤
│ MemoryManager 后台线程 [MM]（可选，默认关闭）                              │
│   - /proc/meminfo 轮询（周期 5 s）                                        │
│   - madvise 调用（实验特性）                                               │
└──────────────────────────────────────────────────────────────────────────┘
```

### 5.2 通信机制

| 方向 | 机制 | 用途 |
| :--- | :--- | :--- |
| W → M | `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` | 进度回报、任务完成、错误通知 |
| M → W | `QtConcurrent::run` 提交任务 | 索引、匹配、搜索 |
| W → W | `std::atomic` | 进度计数、取消令牌 |
| M → M | 直接调用 | MarkerManager、UI 状态更新 |
| 渲染 → DAL | 原子指针加载（`shared_ptr` 快照） | 读取 TextBuffer / FilterResult |

**MUST**：GUI 主线程**绝不**执行 I/O、正则、`mmap` 系统调用（§0.3 硬约束 3）。

### 5.3 锁协议

```
锁清单（v1.0 全量）：
  1. MainController::m_mutex       —— 保护 m_controller 的线程安全方法
     持锁时长：< 1 ms（仅设置标志位）
  2. FilterEngine 内部             —— 无锁（RuleSet 不可变 + atomic 计数）
  3. TextBuffer 内部               —— 无锁（只读快照 + atomic 发布）
  4. MarkerManager 内部            —— 无锁（主线程独占）
  5. QSettings                     —— Qt 内部锁（不暴露给应用层）
  6. TatSerializer 写入            —— 主线程同步（文件 < 1 MB）

MUST 禁止的锁：
  - 渲染路径（LogViewDelegate::paint）不得加锁
  - 不得在持锁期间提交 QtConcurrent 任务（死锁风险）
  - 不得在 QtConcurrent 回调中加锁（Qt 内部不保证回调线程）
```

### 5.4 双缓冲与 `shared_ptr` 快照

#### 5.4.1 设计

BLUEPRINT §3.1 建议"后台填充 BackBuffer，完成后原子交换指针给 FrontBuffer"，
ARCH-UB §4.1 建议"`std::atomic<std::vector<quint8>*>` 原子交换，`delete` 旧 buffer 延后到下一个事件循环 tick"。

**问题**：裸指针 + 延后 delete 的协议复杂且易出错。若渲染线程在"交换后、delete 前"的某个 tick 读取旧指针，
就会访问已释放内存（use-after-free）。

**决策**：采用 C++17 兼容的 `SharedSnapshot<const FilterResult>` 发布结果（R-01，§1.5），
渲染线程通过 `snapshot()` 取得私有快照（`shared_ptr` 副本），引用计数保证旧对象在最后一个读者析构后才释放。

```cpp
// src/core/engine/ResultStore.h
#pragma once
#include <memory>
#include "FilterResult.h"
#include "common.h"   // SharedSnapshot<T>

namespace tat {

// 发布-订阅式的双缓冲（基于 SharedSnapshot，C++17 兼容，无 use-after-free）。
// 写者（FilterEngine）：在后台构造完 FilterResult 后 publish()（原子替换）
// 读者（LogViewDelegate）：snapshot() 取得私有快照 → 在 paint 内使用
// 旧 FilterResult 在最后一个 shared_ptr 快照析构时释放（可能发生在任意线程）
class ResultStore {
public:
    // 写者：发布新结果。
    void publish(std::shared_ptr<const FilterResult> result) noexcept {
        m_current.publish(std::move(result));
    }

    // 读者：取得当前结果的快照。调用方持有期间结果不会被释放。
    std::shared_ptr<const FilterResult> snapshot() const noexcept {
        return m_current.snapshot();
    }

    // 读者：检查是否需要重绘（generation 变化）
    int currentGeneration() const noexcept {
        auto r = snapshot();
        return r ? r->generation : -1;
    }

    // 写者：清空（文件关闭时）
    void clear() noexcept { m_current.clear(); }

private:
    SharedSnapshot<const FilterResult> m_current;
};

} // namespace tat
```

#### 5.4.2 内存安全证明

```
场景：写者 publish(newResult)，读者在 publish 前后读取。

1. 读者 load() 发生在 publish 之前：
   - 得到 oldResult 的 shared_ptr 副本
   - 引用计数 += 1（现在是 2：store 中的 + 读者的）
   - 写者 publish 后 store 中的 shared_ptr 被替换为 newResult
   - oldResult 的引用计数降为 1（仅剩读者的）
   - 读者析构快照时，引用计数降为 0，oldResult 释放
   ✅ 安全

2. 读者 load() 发生在 publish 之后：
   - 得到 newResult 的 shared_ptr 副本
   - 引用计数 += 1
   - 读者使用 newResult
   ✅ 安全

3. 读者 load() 发生在 publish 期间：
   - `std::atomic_load/atomic_store` 自由函数（libstdc++ 自旋锁）保证原子性
   - 读者要么看到旧值，要么看到新值，不会看到中间状态
   - 语义与 C++20 的 std::atomic<std::shared_ptr<T>> 完全一致（R-01）
   ✅ 安全
```

**与裸指针方案的对比**：

| 维度 | 裸指针 + 延后 delete | `shared_ptr` 快照 |
| :--- | :--- | :--- |
| 内存安全 | 需要精确的 tick 同步 | 自动（引用计数） |
| 代码复杂度 | 高（需手动管理生命周期） | 低（RAII） |
| 内存开销 | 0 | 每次 load 分配一个 control block（~16 B） |
| 分配频率 | 0 | 每次 paint 一次（约 60 fps × 100 行 = 6000 次/s） |
| 性能 | 略优 | 略差（每次 load 一次原子引用计数操作） |

> **决策**：选择 `shared_ptr` 快照。理由：内存安全 > 性能。
> 每次 load 的开销约 50–100 ns，60 fps 下总计 3–6 ms/s，可忽略。
> 若性能不达标，v1.2 可改用 `std::atomic<T*>` + epoch-based reclamation（如 Hazard Pointer）。

#### 5.4.3 渲染路径的快照获取

```cpp
// LogViewDelegate::paint 中（§7.3）
void LogViewDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt,
                            const QModelIndex& index) const {
    // 1. 获取 TextBuffer 快照（atomic load）
    auto buffer = m_controller->bufferSnapshot();   // shared_ptr<const TextBuffer>
    if (!buffer) return;

    // 2. 获取 FilterResult 快照
    auto result = m_controller->resultSnapshot();   // shared_ptr<const FilterResult>
    auto rules  = m_controller->rulesSnapshot();    // shared_ptr<const RuleSet>

    // 3. 用快照进行渲染（期间不会被关闭/替换）
    int row = index.row();
    if (row < 0 || row >= buffer->rowCount()) return;
    std::string_view text = buffer->textAt(row);
    uint8_t stateRaw = result ? result->states[row] : 0;
    RowState rs; rs.raw = stateRaw;
    // ... 绘制 ...
}
```

> **关键点**：`paint` 方法内的所有数据访问都通过快照，不直接访问 `MainController` 的成员。
> 这保证了即使写者正在 publish 新结果，读者也不会看到不一致状态。

#### 5.4.4 旧结果释放时机

```
时间线：
  t0: 写者 publish(result_v2)
      - m_current.store(result_v2)
      - result_v1 的引用计数从 2 降为 1（假设有一个读者持有）

  t1: 读者 paint() 加载 result_v1 的快照（尚未析构）
      - 引用计数从 1 升为 2

  t2: 读者 paint() 结束，析构快照
      - 引用计数从 2 降为 1

  t3: 写者 publish(result_v3)
      - m_current.store(result_v3)
      - result_v2 的引用计数从 2 降为 1（假设有一个读者持有）

  t4: 读者 paint() 加载 result_v2 的快照
      - 引用计数从 1 升为 2

  t5: 读者 paint() 结束，析构快照
      - 引用计数从 2 降为 1

  t6: 写者 publish(result_v4)
      - m_current.store(result_v4)
      - result_v3 的引用计数从 2 降为 1

  ...

  tN: 最后一个持有 result_v1 的读者析构
      - 引用计数降为 0，result_v1 释放
```

**结论**：旧结果的释放时机由最后一个读者的析构决定，无需手动管理。
这与 ARCH-UB §4.1 的"延后到下一个事件循环 tick"目标一致，但实现更简单且安全。

### 5.5 TextBuffer 的原子发布

```cpp
// MainController 内部（R-01：C++17 下用 SharedSnapshot，不用 std::atomic<shared_ptr>）
SharedSnapshot<const TextBuffer> m_buffer;
SharedSnapshot<const RuleSet>    m_rules;

// 文件打开完成后（工作线程）
void MainController::onIndexReady(std::shared_ptr<TextBuffer> buffer) {
    // 1. 切换到新 buffer（原子替换）
    m_buffer.publish(std::move(buffer));
    // 2. 通知 UI 重置模型
    QMetaObject::invokeMethod(m_view, "resetModel", Qt::QueuedConnection);
    // 3. 更新状态栏
    QMetaObject::invokeMethod(m_statusBar, "updateInfo", Qt::QueuedConnection);
}

// 渲染路径读取
std::shared_ptr<const TextBuffer> MainController::bufferSnapshot() const {
    return m_buffer.snapshot();
}
```

**旧 buffer 的释放**：

- 旧 `TextBuffer` 的 `shared_ptr<MemoryMappedFile>` 会调用 `munmap`。
- 若渲染线程正在读取旧 buffer 的某行（`textAt`），`munmap` 会导致 SIGSEGV。
- **解决**：`shared_ptr` 快照保证旧 buffer 在最后一个读者析构后才释放，
  而 `MemoryMappedFile` 的析构在 `TextBuffer` 析构时调用，因此 `munmap` 发生在
  所有读者都释放快照之后。✅ 安全。

### 5.6 取消协议总结

| 任务 | 令牌 | 检查频率 | 取消后行为 |
| :--- | :--- | :--- | :--- |
| 索引 | `Token` | 每 64 KB | 返回 `ErrCode::Cancelled`，UI 提示 |
| 过滤匹配 | `Token` | 每 8 KB | 返回 `ErrCode::Cancelled`，已写入部分保留 |
| 搜索 | `Token` | 每 10000 行 | 返回 `ErrCode::Cancelled`，已找到部分保留 |
| 文件关闭 | `Token` | 同上 | 同索引 |

**MUST**：所有长任务必须检查 `Token`，且检查频率不低于上表。
不检查 Token 的任务会在用户取消后继续占用 CPU。

### 5.7 进度回报协议

```
worker 线程：
  atomic<size_t> doneLines;
  atomic<size_t> lastReportedPercent(0);

  // 每处理一个 chunk
  doneLines.fetch_add(chunkSize, relaxed);
  int percent = doneLines / N * 100;
  if (percent / 10 > lastReportedPercent / 10) {
      lastReportedPercent.store(percent);
      // 通过注入的回调通知主线程
      onProgress(Progress{percent, doneLines, N, elapsedMs});
  }

主线程（MainController）：
  // onProgress 回调
  void onProgress(const Progress& p) {
      QMetaObject::invokeMethod(m_progressDialog, "setProgress",
                                Qt::QueuedConnection, Q_ARG(double, p.percent));
  }
```

**MUST**：进度回报频率不超过 10 次（每 10% 一次），避免主线程过载。

---

## 6. 控制层（Controller）

### 6.1 `MainController` 职责

`MainController` 是 UI 层与 BLL/DAL 之间的唯一桥梁（中介者模式）。

**职责**：

1. 持有 `TextBuffer`、`FilterEngine`、`MarkerManager`、`Searcher` 的引用。
2. 接收 UI 信号（按钮点击、快捷键、双击），路由到 BLL/DAL。
3. 接收 BLL/DAL 的回调（进度、完成、错误），转发到 UI。
4. 管理双缓冲发布（`ResultStore`、`m_buffer` 原子指针）。
5. 管理会话（`QSettings` 读写）。
6. 管理命令行动作（`--grep`、`--line`、`--load`、`--export`）。

**MUST**：`MainController` 通过 Qt 信号对外暴露命令方法，禁止跨层直接调用 BLL。

### 6.2 接口

```cpp
// src/controller/MainController.h
#pragma once
#include <QObject>
#include <QPointer>
#include <atomic>
#include <memory>
#include <mutex>
#include "FilterRule.h"
#include "FilterResult.h"
#include "MarkerManager.h"
#include "ResultStore.h"
#include "TextBuffer.h"

namespace tat {

class MainWindow;
class SettingsManager;
class FileWatcher;

class MainController : public QObject {
    Q_OBJECT
public:
    explicit MainController(QObject* parent = nullptr);
    ~MainController();

    // ===== 命令（M 调用）=====

    // 打开文件。异步：立即返回，完成后通过 fileOpened 信号通知。
    void openFile(const QString& path);

    // 提交过滤任务。异步：立即返回，完成后通过 filterApplied 信号通知。
    // 内部处理取消协议（§5.6）。
    void applyFilter(const std::vector<FilterRule>& rules);

    // 切换标记（主线程同步，O(log n)）
    bool toggleMarker(int line, int markerId);

    // 跳转到行号（1-based）。返回 false 表示行号越界。
    bool jumpToLine(int lineNo);

    // 跳转到下一个标记
    bool jumpToNextMarker(int lineNo, int markerId);

    // 搜索。异步：立即返回，完成后通过 searchCompleted 信号通知。
    void search(const SearchOptions& opt);

    // 导出选中行到文件。同步（文件 < 10 MB 时）。
    Error exportLines(int startLine, int endLine, const QString& path);

    // 加载/保存 .tat
    Error loadFilters(const QString& path, std::vector<FilterRule>* out);
    Error saveFilters(const QString& path, const std::vector<FilterRule>& rules);

    // ===== 快照（渲染路径调用，无锁）=====

    std::shared_ptr<const TextBuffer> bufferSnapshot() const noexcept;
    std::shared_ptr<const FilterResult> resultSnapshot() const noexcept;
    std::shared_ptr<const RuleSet> rulesSnapshot() const noexcept;

    // ===== 状态查询 =====

    int  rowCount() const noexcept;
    int  currentLine() const noexcept;
    TextBuffer::MemoryFootprint memoryFootprint() const;

    // ===== 会话管理 =====

    void saveSession() const;
    void restoreSession();

    // ===== 注入的回调（构造时设置）=====

    void setCompileFn(CompileFn fn);
    void setMatchFn(MatchFn fn);

signals:
    void fileOpened(const QString& path, int rowCount);
    void fileOpenFailed(const QString& path, const Error& err);
    void filterApplied(int rowCount, const Error& err);
    void filterProgress(double percent);
    void searchCompleted(const SearchResult& result);
    void searchProgress(double percent);
    void markerToggled(int line, int markerId, bool added);
    void bufferChanged();          // TextBuffer 已替换，UI 需 resetModel
    void statusMessage(const QString& msg);
    void regexTimeout(int ruleId, const QString& pattern);

private slots:
    void onIndexReady(std::shared_ptr<TextBuffer> buffer);
    void onFilterReady(std::shared_ptr<FilterResult> result);
    void onProgress(const Progress& p);
    void onExternalFileChanged(const QString& path);
    // R-14 修订：双击行处理移至 UI 层（MainWindow::onLineDoubleClicked →
    // 预填过滤面板），控制层不再提供 onLineActivated。

private:
    void cleanupBuffers();
    void submitFilterTask(const RuleSet& rules);
    void submitSearchTask(const SearchOptions& opt);

    // 核心成员（R-01：一律 SharedSnapshot，C++17 兼容）
    SharedSnapshot<const TextBuffer> m_buffer;
    ResultStore                                    m_resultStore;
    SharedSnapshot<const RuleSet>    m_rules;
    MarkerManager                                  m_markers;

    // 任务管理
    Token                                          m_token;
    std::mutex                                     m_taskMutex;
    ThreadPool*                                    m_pool = nullptr;
    CompileFn                                      m_compile;
    MatchFn                                        m_match;

    // 持久化
    SettingsManager*                               m_settings = nullptr;
    FileWatcher*                                   m_watcher  = nullptr;
    QPointer<MainWindow>                           m_window;

    // 状态
    int                                            m_currentLine = 1;
    QString                                        m_currentPath;
};

} // namespace tat
```

### 6.3 命令路由

| UI 信号/动作 | Controller 方法 | BLL/DAL 调用 | 结果 |
| :--- | :--- | :--- | :--- |
| 工具栏"打开" | `openFile(path)` | `TextBuffer::create` | `fileOpened` / `fileOpenFailed` |
| 过滤器列表编辑 | `applyFilter(rules)` | `FilterEngine::applyParallel` | `filterApplied` |
| 过滤器输入框 200 ms | `applyFilter(rules)` | 同上（带取消） | `filterApplied` |
| 双击行 | R-14 修订：`MainWindow::onLineDoubleClicked` → 预填过滤面板 Text 输入框（可编辑），**不**自动应用 | 面板预填 | 面板 Text 更新 |
| `Ctrl+1..8` | `toggleMarker` | `MarkerManager::toggle` | `markerToggled` |
| `Alt+1..8` | `jumpToNextMarker` | `MarkerManager::next` | 视图滚动 |
| 搜索框 Enter | `search(opt)` | `Searcher::search` | `searchCompleted` |
| 搜索框输入 | `searchViewport` | `Searcher::searchViewport` | 下拉更新 |
| 文件外部修改 | `onExternalFileChanged` | 状态栏提示 | `statusMessage` |
| `--grep` | `applyFilter(ruleFromGrep)` | 同上 | `filterApplied` |
| `--export` | `exportLines` | 主线程同步 | 文件写入 |

### 6.4 会话恢复

```cpp
void MainController::restoreSession() {
    if (!m_settings->value("session/restore", true).toBool()) return;
    if (m_settings->value("session/noRestore", false).toBool()) return;

    // 1. 恢复窗口几何
    QByteArray geo = m_settings->value("geometry/main").toByteArray();
    if (!geo.isEmpty()) m_window->restoreGeometry(geo);

    // 2. 恢复 Dock 状态
    QByteArray state = m_settings->value("state/main").toByteArray();
    if (!state.isEmpty()) m_window->restoreState(state);

    // 3. 恢复最近文件
    QStringList recent = m_settings->value("recentFiles").toStringList();
    if (!recent.isEmpty()) {
        openFile(recent.first());
    }

    // 4. 恢复标记
    QStringList markerStrs = m_settings->value("session/markers").toStringList();
    for (const QString& s : markerStrs) {
        QList<QString> parts = s.split(':');
        if (parts.size() == 2) {
            int line = parts[0].toInt();
            int id = parts[1].toInt();
            m_markers.add(line, id);
        }
    }

    // 5. 恢复过滤规则（从 .tat 库）
    QString tatPath = m_settings->value("filter/library").toString();
    if (!tatPath.isEmpty() && QFile::exists(tatPath)) {
        std::vector<FilterRule> rules;
        loadFilters(tatPath, &rules);
        if (!rules.empty()) applyFilter(rules);
    }
}
```

### 6.5 命令行解析

```cpp
// src/controller/CommandLineParser.h
#pragma once
#include <QCommandLineParser>
#include <QString>
#include <QStringList>

namespace tat {

struct CommandLineOptions {
    QString file;
    int     line = 0;
    QString grep;
    QString load;
    QString exportPath;
    bool    noRestore = false;
    bool    verbose   = false;
    bool    compactMemory = false;
};

class CommandLineParser {
public:
    static CommandLineOptions parse(const QStringList& args, bool* ok);
    static QString helpText();
};

} // namespace tat
```

**解析流程**：

```
parse(args, ok):
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption({"file", "Open file", "path"});
  parser.addOption({"line", "Jump to line", "n"});
  parser.addOption({"grep", "Filter by pattern", "pattern"});
  parser.addOption({"load", "Load rules from .tat", "file"});
  parser.addOption({"export", "Export matched lines", "file"});
  parser.addOption({"no-restore", "Skip last-session restore"});
  parser.addOption({"verbose", "Verbose logging"});
  parser.addOption({"compact-memory", "Enable MADV_DONTNEED (experimental)"});
  parser.addPositionalArgument("file", "Open file", "[file]");
  parser.process(args);

  opts.file = parser.value("file") or positionalArgument
  opts.line = parser.value("line").toInt()
  opts.grep = parser.value("grep")
  opts.load = parser.value("load")
  opts.exportPath = parser.value("export")
  opts.noRestore = parser.isSet("no-restore")
  opts.verbose = parser.isSet("verbose")
  opts.compactMemory = parser.isSet("compact-memory")
```

**启动流程**：

```
main():
  1. QApplication app(argc, argv)
  2. QCoreApplication::setOrganizationName("textanalyst-qt")
     QCoreApplication::setApplicationName("textanalyst-qt")
     QCoreApplication::setApplicationVersion("1.0.0")
  3. installMessageHandler(&crashLogger)       // §11.2
  4. installSignalHandlers()                   // §11.4
  5. opts = CommandLineParser::parse(args, &ok)
  6. window = new MainWindow(opts)
  7. window->show()
  8. if opts.file: controller->openFile(opts.file)
     if opts.line: controller->jumpToLine(opts.line)
     if opts.grep: controller->applyFilter(ruleFromGrep(opts.grep))
     if opts.load: controller->loadFilters(opts.load)
     if opts.exportPath: controller->exportLines(...)
  9. app.exec()
```

### 6.6 信号处理与优雅退出

```cpp
// src/main.cpp
#include <csignal>
#include <QCoreApplication>
#include <QTimer>

static void signalHandler(int sig) {
    // 通过 QTimer::singleShot 调度到主线程，避免在非主线程调用 Qt API
    QTimer::singleShot(0, []() {
        QCoreApplication::quit();
    });
}

static void installSignalHandlers() {
    struct sigaction sa{};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP,  &sa, nullptr);
}
```

**退出流程**：

```
QCoreApplication::quit()
  → MainWindow::closeEvent()
    → controller->saveSession()      // 保存窗口几何、最近文件、标记、规则
    → controller->cleanupBuffers()   // 释放 mmap、取消进行中的任务
    → event->accept()
```

**MUST**：退出前保存会话（§0.3 硬约束，ARCH-UB §6.3）。

---

## 7. UI 表现层

### 7.1 架构总览

```
MainWindow (QMainWindow)
  ├── QToolBar (顶部工具条)
  │     ├── 打开 / 保存 / 关闭 / 导出
  │     └── 搜索 / 过滤 / 标记
  ├── LogListView (中央，QListView 子类)
  │     ├── LogViewModel (QAbstractListModel)
  │     └── LogViewDelegate (QStyledItemDelegate)
  ├── FilterDockWidget (右侧 QDockWidget)
  │     └── QListView + FilterListModel
  ├── QStatusBar (底部)
  │     ├── 文件路径
  │     ├── 行数 / 当前行
  │     ├── 内存占用
  │     └── 正则耗时
  └── QDialogs
        ├── FindDialog
        ├── PreferencesDialog
        └── AboutDialog
```

### 7.2 `LogViewModel`

```cpp
// src/ui/widgets/LogViewModel.h
#pragma once
#include <QAbstractListModel>
#include <memory>

namespace tat {

class TextBuffer;

// 只读模型。rowCount() 返回 TextBuffer 的总行数（包含隐藏行）。
class LogViewModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit LogViewModel(std::shared_ptr<const TextBuffer> buffer, QObject* parent = nullptr);

    void setBuffer(std::shared_ptr<const TextBuffer> buffer);
    std::shared_ptr<const TextBuffer> buffer() const noexcept { return m_buffer; }

    // QAbstractListModel 接口
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 自定义角色
    enum Roles {
        LineNoRole = Qt::UserRole + 1,
        StateRole,        // ResultState
        RuleRefRole,      // ruleRef (int)
        TextRole,         // QString（已转码）
        HitRole,          // SearchHit*（当前行是否有搜索命中）
    };

    // 行高（固定，§7.2.2）
    int rowHeight() const noexcept { return m_rowHeight; }

private:
    std::shared_ptr<const TextBuffer> m_buffer;
    int m_rowHeight = 16;
};

} // namespace tat
```

#### 7.2.1 数据访问

```cpp
QVariant LogViewModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !m_buffer) return {};
    int row = index.row();

    switch (role) {
    case Qt::DisplayRole: {
        // 返回已转码的字符串（§7.3 的 LRU 缓存）
        return m_buffer->toUtf8(row);
    }
    case LineNoRole:
        return row + 1;   // 1-based
    case StateRole: {
        // 由 Delegate 直接读取 ResultStore，不经过 Model
        // 此处返回 -1 表示"未设置"
        return -1;
    }
    case TextRole: {
        // 返回原始 UTF-8 字符串（零拷贝）
        auto sv = m_buffer->textAt(row);
        return QString::fromUtf8(sv.data(), sv.size());
    }
    default:
        return {};
    }
}
```

#### 7.2.2 固定行高

```cpp
int LogViewModel::rowHeight() const {
    // 由字体决定。初始化时计算一次。
    QFontMetrics fm(m_font);
    return fm.height() + 2;   // +2 行间距
}
```

**MUST**：所有行使用固定高度（`setUniformItemSizes(true)`），避免 `sizeHint` 被逐行调用。
对超宽行（> 视口宽度），通过水平滚动支持，而非换行（§7.3.4）。

### 7.3 `LogViewDelegate`（核心渲染）

#### 7.3.1 接口

```cpp
// src/ui/widgets/LogViewDelegate.h
#pragma once
#include <QStyledItemDelegate>
#include <QHash>
#include <memory>

namespace tat {

class MainController;

class LogViewDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit LogViewDelegate(MainController* controller, QWidget* parent = nullptr);

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

    // 设置搜索高亮（由 FindDialog 更新）
    void setSearchHits(const std::vector<SearchHit>& hits);
    void clearSearchHits();

private:
    // 颜色解析（§3.3 契约）
    void resolveColors(uint8_t stateRaw, const RuleSet& rules,
                       Argb* fg, Argb* bg) const;

    // 转码缓存（LRU）
    QString transcode(int row, const TextBuffer& buffer) const;

    MainController* m_controller;
    mutable QHash<int, QString> m_transcodeCache;   // LRU，容量 4096
    std::vector<SearchHit>      m_searchHits;
    mutable QHash<int, QVector<int>> m_hitOffsets;   // 行号 → 字符偏移列表
    int m_maxPaintWidth = 16384;   // 单行最大绘制字符数（§7.3.4）
};

} // namespace tat
```

#### 7.3.2 渲染流程

```
LogViewDelegate::paint(p, opt, index):
  1. row = index.row()
     if row < 0 or row >= buffer->rowCount(): return

  2. buffer = m_controller->bufferSnapshot()       // atomic load
     result = m_controller->resultSnapshot()       // atomic load
     rules  = m_controller->rulesSnapshot()        // atomic load
     if !buffer: return

  3. text = transcode(row, *buffer)                // §7.3.3
     if text.isEmpty(): return                      // 空行不绘制（但保留行槽）

  4. stateRaw = result ? result->states[row] : 0
     RowState rs; rs.raw = stateRaw

  5. if rs.isHidden():
       // 不绘制内容，但绘制行号（§7.3.5）
       paintLineNumber(p, opt, row)
       return

  6. resolveColors(stateRaw, *rules, &fg, &bg)

  7. // 绘制背景
  QRect rect = opt.rect.adjusted(0, 0, -1, -1)
  if bg != 0x00000000:
      p->fillRect(rect, bg)

  8. // 绘制搜索高亮（下划线）
  if m_searchHits 包含 row:
      drawSearchUnderlines(p, rect, row, *buffer)

  9. // 绘制文本（截断超宽行）
  p->setPen(fg)
  int maxChars = m_maxPaintWidth
  QString displayText = text.left(maxChars)
  p->drawText(opt.rect.adjusted(4, 0, -4, 0), Qt::TextSingleLine, displayText)

 10. // 绘制行号
  paintLineNumber(p, opt, row)

 11. // 绘制标记指示器
  drawMarkerIndicator(p, opt, row)
```

#### 7.3.3 转码与 LRU 缓存

```cpp
QString LogViewDelegate::transcode(int row, const TextBuffer& buffer) const {
    // 1. 检查缓存
    auto it = m_transcodeCache.find(row);
    if (it != m_transcodeCache.end()) {
        return it.value();
    }

    // 2. 转码
    QString text = buffer.toUtf8(row);

    // 3. 写入缓存（LRU 淘汰）
    if (m_transcodeCache.size() >= 4096) {
        // 简化实现：清空后重建（实际应使用 QCache 或自实现 LRU）
        m_transcodeCache.clear();
    }
    m_transcodeCache.insert(row, text);
    return text;
}
```

> **性能注记**：LRU 容量 4096 条，覆盖视口（约 100 行）的 40 倍。
> 滚动时命中率高（相邻行），内存占用约 4096 × 64 B ≈ 256 KB。
> 清空式淘汰是简化实现，v1.1 改用 `QCache` 或自实现 LRU 链表。

#### 7.3.4 超宽行处理

```
单行最大绘制字符数：m_maxPaintWidth = 16384
超宽行（> 16384 字符）：
  - 绘制前 16384 字符
  - 在末尾绘制 "..." 表示截断
  - 水平滚动支持剩余部分（QListView::setHorizontalScrollBarPolicy(ScrollAsNeeded)）

单行最大字节数：m_maxBytesPerLine = 1 MiB
超宽字节（> 1 MiB）：
  - 转码时截断到 1 MiB
  - 提示用户"行过长，已截断显示"
```

**MUST**：`paint` 内不得进行正则匹配或复杂计算（§0.3 硬约束，ARCH-UB §5.1）。

#### 7.3.5 隐藏行的显示

```
Hidden 状态：
  - 不绘制文本内容
  - 绘制行号（灰色，#808080）
  - 绘制背景（与 Normal 相同，保持视觉连续性）
  - 行高不变（保持原始行号对齐）

Dimmed 状态：
  - 绘制文本（灰色前景 #808080）
  - 不绘制背景（透明）
  - 行高不变

Highlighted 状态：
  - 绘制文本（规则前景色）
  - 绘制背景（规则背景色）
  - 行高不变
```

**决策**：隐藏行保留行槽与行号，而非折叠或删除。
理由：日志分析需要对照原始行号，折叠行号会导致用户无法定位原始位置。
这与原版 TAT 的行为一致。

#### 7.3.6 颜色叠加

```
颜色叠加顺序（后绘制覆盖先绘制）：
  1. 背景（规则背景色或 Normal 背景色）
  2. 搜索高亮（下划线，不覆盖背景）
  3. 文本（规则前景色或 Dimmed 灰色）
  4. 标记指示器（左侧 4px 色条，覆盖所有）
  5. 行号（右侧固定列，灰色或黑色）
```

### 7.4 `LogListView`

```cpp
// src/ui/widgets/LogListView.h
#pragma once
#include <QListView>
#include <QScrollBar>

namespace tat {

class LogViewModel;

class LogListView : public QListView {
    Q_OBJECT
public:
    explicit LogListView(QWidget* parent = nullptr);

    void setModel(LogViewModel* model);
    void scrollToLine(int lineNo);
    void scrollToNextHit(int direction);   // direction: +1 下一个，-1 上一个

    int  currentLine() const noexcept;
    void setCurrentLine(int lineNo);

signals:
    void lineActivated(int lineNo);        // 双击或 Enter
    void lineEntered(int lineNo);          // 鼠标进入
    void searchRequested();                // Ctrl+F

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void viewportEvent(QEvent* e) override;

private:
    int hitTest(const QPoint& pos) const;
    int findLineAt(const QPoint& pos) const;

    LogViewModel* m_model = nullptr;
    int m_currentLine = 1;
};

} // namespace tat
```

#### 7.4.1 初始化

```cpp
LogListView::LogListView(QWidget* parent) : QListView(parent) {
    // 固定行高（§7.2.2）
    setUniformItemSizes(true);
    setResizeMode(QListView::Adjust);

    // 滚动策略
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    // 选择行为
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionRectVisible(true);

    // 键盘导航
    setFocusPolicy(Qt::StrongFocus);
    setEditTriggers(QAbstractItemView::NoEditTriggers);

    // 性能：禁用 QListView 的内部缓存
    setUpdateMode(QAbstractItemView::UniformRowHeights);
}
```

#### 7.4.2 快捷键

| 快捷键 | 动作 | 实现 |
| :--- | :--- | :--- |
| `Ctrl+O` | 打开文件 | `triggerAction(openAction)` |
| `Ctrl+S` | 保存 .tat | `triggerAction(saveAction)` |
| `Ctrl+F` | 搜索 | `triggerAction(findAction)` |
| `Ctrl+G` | 跳转到行 | `triggerAction(gotoAction)` |
| `Ctrl+1..8` | 切换标记 | `controller->toggleMarker(currentLine, n)` |
| `Alt+1..8` | 跳转标记 | `controller->jumpToNextMarker(currentLine, n)` |
| `Ctrl+N` / `Ctrl+P` | 下一个/上一个命中 | `scrollToNextHit(+1/-1)` |
| `Ctrl+Home` | 跳到首行 | `scrollToLine(1)` |
| `Ctrl+End` | 跳到末行 | `scrollToLine(rowCount)` |
| `Esc` | 取消搜索/过滤 | `cancelCurrentTask()` |
| `F5` | 重新应用过滤 | `controller->applyFilter(currentRules)` |
| `Ctrl+H` | 替换（v1.1） | — |

#### 7.4.3 滚动优化

```
viewportEvent(e):
  if e->type() == QEvent::Paint:
      // 仅绘制视口内的行（QListView 内部已实现）
      pass through

  if e->type() == QEvent::Wheel:
      // 鼠标滚轮：快速滚动
      // QListView 内部处理，无需额外优化
      pass through
```

**MUST**：`QListView::setUniformItemSizes(true)` 启用均匀行高优化，
避免滚动时逐行计算 `sizeHint`。

### 7.5 `FilterListModel` 与 `FilterDockWidget`

#### 7.5.1 `FilterListModel`

```cpp
// src/ui/widgets/FilterListModel.h
#pragma once
#include <QAbstractTableModel>
#include <QVector>

namespace tat {

// 过滤器规则的可编辑模型。
// 列：0=Enabled, 1=Color, 2=Mode, 3=Pattern, 4=MatchCount, 5=Rank
class FilterListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Columns {
        ColEnabled = 0, ColColor, ColMode, ColPattern, ColCount, ColRank, ColumnCount
    };

    explicit FilterListModel(QObject* parent = nullptr);

    // QAbstractTableModel 接口
    int rowCount(const QModelIndex& = {}) const override;
    int columnCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool     setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

    // 增删改
    void addRule(const FilterRule& rule);
    void removeRule(int row);
    void moveRule(int from, int to);

    // 查询
    FilterRule ruleAt(int row) const;
    std::vector<FilterRule> rules() const;

    // 更新命中计数（由 FilterEngine 回调）
    void setMatchCount(int row, int count);

signals:
    void rulesChanged();
    void ruleAdded(int row);
    void ruleRemoved(int row);

private:
    QVector<FilterRule> m_rules;
};

} // namespace tat
```

#### 7.5.2 `FilterDockWidget`

```cpp
// src/ui/widgets/FilterDockWidget.h
#pragma once
#include <QDockWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>

namespace tat {

class FilterListModel;

class FilterDockWidget : public QDockWidget {
    Q_OBJECT
public:
    explicit FilterDockWidget(QWidget* parent = nullptr);

    void setModel(FilterListModel* model);
    FilterListModel* model() const noexcept { return m_model; }

signals:
    void rulesApplied();   // 用户点击"应用"

private slots:
    void onAddRule();
    void onRemoveRule();
    void onPatternChanged(const QString& text);
    void onModeChanged(int mode);
    void onColorChanged(const QColor& color);

private:
    FilterListModel* m_model;

    // 输入栏
    QLineEdit*   m_patternEdit;
    QCheckBox*   m_includeCheck;
    QCheckBox*   m_regexCheck;
    QCheckBox*   m_caseCheck;
    QPushButton* m_colorButton;
    QColor       m_color = QColor(0xFF000000);
    QTimer*      m_debounceTimer;   // 200 ms 防抖
};

} // namespace tat
```

**R-13/R-22 布局（原版 TAT 三行表单 + 弹窗化编辑）**：

编辑表单位于 `FilterRuleDialog` 弹窗（添加/修改共用，自上而下）：

```
第1行: [Filter 下拉: Matches text/Contains/Starts with/Ends with]
       [Text Color 下拉] [Background 下拉]
第2行: [Text: 单行输入（双击文本行预填，R-14）]
第3行: [Description: 单行输入（规则备注，R-18）]
底部:  [x] Excluding [!]   [x] Case-sensitive [Aa]   [x] Regular expression [R]
       确定 / 取消
```

触发路径（同一弹窗、两种模式）：

| 入口 | 模式 | 预填 |
| :--- | :--- | :--- |
| 双击日志行 | 添加（editId=-1） | 该行文本 |
| Filters 菜单 Add New Filter...（Ctrl+N） | 添加 | 空表单 |
| 双击规则列表条目 / Edit Selected Filter... | 修改（editId=规则id） | 该规则现有配置 |

底部常驻面板为**纯列表**（无操作按钮，R-23），可经 View 菜单关闭/恢复；
规则操作全部在 Filters 菜单：匹配跳转（F8/Shift+F8）、添加/修改/删除
选中、全部启用/禁用/清除。编辑表单不再占常驻空间。

面板值 → `FilterRule` 映射：Filter 下拉 → `matchType`；Text → `pattern`；
Description → `description`；三个复选框 → `action`/`caseSensitive`/`mode`；
颜色下拉 → `foreground`/`background`。

**防抖历史注记**（§0.4 #6，ARCH-UB §5.4 — v1.0 为自动应用设计的防抖，在
R-13 面板形态下由显式"添加"按钮取代，防抖机制保留于 FindDialog 输入场景）：

```cpp
void FilterDockWidget::onPatternChanged(const QString& text) {
    // 200 ms 防抖
    m_debounceTimer->start(200);
}

void FilterDockWidget::onDebounceTimeout() {
    // 构造规则并应用
    FilterRule rule;
    rule.pattern = m_patternEdit->text().toStdString();
    rule.action = m_includeCheck->isChecked() ? FilterAction::Include : FilterAction::Exclude;
    rule.mode = m_regexCheck->isChecked() ? MatchMode::Regex : MatchMode::Substring;
    rule.caseSensitive = m_caseCheck->isChecked();
    rule.foreground = m_color.rgb();
    m_model->addRule(rule);
    emit rulesApplied();
}
```

**MUST**：使用 `QTimer::singleShot` 或 `QTimer::start`，禁止自定义 debounce 类（§0.4 #6）。

### 7.6 `MainWindow`

```cpp
// src/ui/mainwindow/MainWindow.h
#pragma once
#include <QMainWindow>
#include <QSettings>

namespace tat {

class MainController;
class LogListView;
class LogViewModel;
class LogViewDelegate;
class FilterDockWidget;
class FilterListModel;
class FindDialog;
struct CommandLineOptions;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const CommandLineOptions& opts, QObject* parent = nullptr);
    ~MainWindow();

    void openFile(const QString& path);

signals:
    void resetModel();

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void setupUI();
    void setupActions();
    void setupStatusBar();
    void loadSettings();
    void saveSettings();
    void updateStatusBar();

    // 核心成员
    MainController*   m_controller;
    LogListView*      m_view;
    LogViewModel*     m_model;
    LogViewDelegate*  m_delegate;
    FilterDockWidget* m_filterDock;
    FilterListModel*  m_filterModel;
    FindDialog*       m_findDialog;
    QStatusBar*       m_statusBar;

    // 状态栏 widgets
    QLabel*           m_pathLabel;
    QLabel*           m_rowCountLabel;
    QLabel*           m_currentLineLabel;
    QLabel*           m_memoryLabel;
    QLabel*           m_regexTimeLabel;
};

} // namespace tat
```

#### 7.6.1 UI 构建流程

```
MainWindow::setupUI():
  1. // 工具条
     toolbar = addToolBar("Main")
     toolbar->addAction(openAction)
     toolbar->addAction(saveAction)
     toolbar->addAction(closeAction)
     toolbar->addSeparator()
     toolbar->addAction(findAction)
     toolbar->addAction(filterAction)
     toolbar->addSeparator()
     toolbar->addAction(exportAction)

  2. // 中央视图
     m_model = new LogViewModel(nullptr, this)
     m_delegate = new LogViewDelegate(m_controller, this)
     m_view = new LogListView(this)
     m_view->setModel(m_model)
     m_view->setItemDelegate(m_delegate)
     setCentralWidget(m_view)

  3. // 过滤器停靠窗
     m_filterModel = new FilterListModel(this)
     m_filterDock = new FilterDockWidget(this)
     m_filterDock->setModel(m_filterModel)
     addDockWidget(Qt::RightDockWidgetArea, m_filterDock)

  4. // 状态栏
     setupStatusBar()

  5. // 搜索对话框（延迟创建）
     // m_findDialog = new FindDialog(this)  // 在 Ctrl+F 时创建

  6. // 加载设置
     loadSettings()

  7. // 连接信号
     // R-14 修订：双击只预填过滤面板 Text 输入框，不再隐式应用规则
     connect(m_view, &LogListView::lineActivated, this, &MainWindow::onLineDoubleClicked)
     connect(m_filterDock, &FilterDockWidget::rulesApplied, m_controller, &MainController::applyFilter)
     connect(m_controller, &MainController::bufferChanged, this, &MainWindow::resetModel)
     connect(m_controller, &MainController::fileOpened, this, &MainWindow::onFileOpened)
```

#### 7.6.2 状态栏

```
状态栏布局（从左到右）：
  [文件路径] | [行数: N] | [当前行: M] | [内存: X MB] | [正则: Y ms]

更新频率：
  - 文件路径：文件打开时
  - 行数：文件打开时
  - 当前行：每次滚动/点击
  - 内存：每 1 s 更新（QTimer）
  - 正则耗时：每次过滤完成时
```

### 7.7 搜索对话框

```cpp
// src/ui/dialogs/FindDialog.h
#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>

namespace tat {

class FindDialog : public QDialog {
    Q_OBJECT
public:
    explicit FindDialog(QWidget* parent = nullptr);

    SearchOptions options() const;
    void setOptions(const SearchOptions& opt);

signals:
    void searchRequested(const SearchOptions& opt);
    void nextHitRequested(int direction);   // +1 下一个，-1 上一个
    void cancelRequested();

private slots:
    void onSearchTextChanged(const QString& text);
    void onSearchModeChanged(int mode);
    void onFindNext();
    void onFindPrev();

private:
    QLineEdit* m_patternEdit;
    QCheckBox* m_caseCheck;
    QCheckBox* m_regexCheck;
    QCheckBox* m_wholeWordCheck;
    QLabel*    m_resultLabel;   // "找到 N 处"
};

} // namespace tat
```

**交互流程**：

```
1. 用户按 Ctrl+F → 打开 FindDialog
2. 用户输入 pattern → 200 ms 防抖 → 增量搜索（§3.5.5）
3. 用户按 Enter → 全量搜索（后台线程）
4. 搜索完成 → 更新 resultLabel（"找到 N 处"）
5. 用户按 F3 / Shift+F3 → 下一个/上一个命中
6. 用户按 Esc → 关闭对话框，取消搜索
```

---

## 8. 错误处理与 UI 反馈

### 8.1 错误传播链

```
DAL 错误 → TextBuffer::create 返回 Error
  → MainController::onIndexReady 接收
  → 主线程通过 fileOpenFailed 信号通知 MainWindow
  → MainWindow 显示 QMessageBox::critical

BLL 错误 → FilterEngine::applyParallel 返回 Error
  → MainController::onFilterReady 接收
  → 主线程通过 filterApplied 信号通知
  → MainWindow 显示 QMessageBox::warning 或状态栏提示

UI 错误 → 用户输入非法正则
  → CompileFn 返回 -1
  → FilterEngine 返回 ErrCode::RegexError
  → MainController 通知 MainWindow
  → FindDialog 在 pattern 输入框下方显示错误信息
```

**MUST**：所有错误必须到达用户可见的 UI 反馈，不得静默吞掉。

### 8.2 错误码与 UI 响应对照表

| 错误码 | 触发场景 | UI 响应 | 恢复策略 |
| :--- | :--- | :--- | :--- |
| `FileNotFound` | 文件路径不存在 | `QMessageBox::critical` | 保留旧文件 |
| `PermissionDenied` | 无读权限 | `QMessageBox::warning` + 状态栏 | 提示 `chmod +r` |
| `MapFailed` | `mmap` 失败 | `QMessageBox::critical` | 检查磁盘空间 |
| `OutOfMemory` | `LineMeta` 分配失败 | `QMessageBox::critical` | 关闭文件释放内存 |
| `EncodingUnknown` | 编码检测失败 | 状态栏提示 | 降级为 Local8Bit |
| `RegexError` | 正则编译失败 | 输入框下方红字提示 | 用户修改正则 |
| `Timeout` | 正则运行 > 3 s | `QMessageBox::question` | 用户选择停止或继续 |
| `Cancelled` | 用户取消 | 状态栏"已取消" | 保留部分结果 |
| `XmlError` | .tat 解析失败 | `QMessageBox::warning` | 保留旧规则 |
| `UnsupportedFormat` | 文件 > 4 GiB | `QMessageBox::warning` | 提示用分段模式 |
| `IoError` | 文件 I/O 失败 | `QMessageBox::critical` | 检查磁盘 |
| `UnsupportedFormat` | 不支持的编码 | 状态栏提示 | 降级处理 |

### 8.3 正则超时处理

```
FilterEngine::applyParallel 中：
  chunkStartMs = now()
  每 8 KB 检查：
    if now() - chunkStartMs > 3000:
      emit regexTimeout(ruleId, pattern)
      return ErrCode::Timeout

MainController::onRegexTimeout(ruleId, pattern):
  // 主线程弹窗
  QMessageBox msgBox
  msgBox.setWindowTitle("正则超时")
  msgBox.setText(QString("正则可能过慢：\n%s\n\n是否停止匹配？").arg(pattern))
  msgBox.setStandardButtons(QMessageBox::Stop | QMessageBox::Continue)
  if msgBox.exec() == QMessageBox::Stop:
    m_token.cancel()
  // 若用户选择继续，忽略，继续跑（最多再跑 3 s 后再次弹窗）
```

### 8.4 崩溃日志

```cpp
// src/main.cpp
#include <QFile>
#include <QDateTime>
#include <QStandardPaths>

static void crashLogger(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    // 1. 输出到 stderr
    fprintf(stderr, "%s\n", msg.toUtf8().constData());

    // 2. 写入崩溃日志文件（仅 Warning 和 Critical）
    if (type >= QtWarningMsg) {
        QString dir = QStandardPaths::writableLocation(QStandardPaths::AppCacheLocation);
        QDir().mkpath(dir);
        QFile f(dir + "/crash.log");
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&f);
            out << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
                << " [" << QtMsgTypeToString(type) << "]"
                << ctx.file << ":" << ctx.line
                << " " << msg << "\n";
        }
    }
}

// 在 main() 开头安装
QCoreApplication::installMessageHandler(&crashLogger);
```

**MUST**：崩溃日志写入 `~/.cache/textanalyst-qt/crash.log`（§0.3 硬约束 5）。
日志文件超过 10 MB 时自动轮转（重命名为 `crash.log.1`，新建 `crash.log`）。

### 8.5 异常安全

**MUST**：

- BLL/DAL 不使用 C++ 异常（§1.4）。
- UI 层的 Qt 对象断言失败（`qFatal`）由 `crashLogger` 捕获。
- `QtConcurrent::run` 中的任务不得抛异常（BLL 已保证）。
- `std::bad_alloc` 由 `try/catch` 在 `TextBuffer::create` 中捕获，转为 `ErrCode::OutOfMemory`。

```cpp
// TextBuffer::create 中
try {
    m_lines.resize(estimated);
} catch (const std::bad_alloc&) {
    return Error{ErrCode::OutOfMemory, "LineMeta allocation", "not enough memory"};
}
```

---

## 9. 性能预算与内存分解

### 9.1 性能预算（与 ARCH-UB §13 一致）

测试硬件：Ubuntu 24.04，x86_64，8 vCPU，16 GB RAM，NVMe SSD。

| 指标 | 目标 | 测量方法 | CI 阻断 |
| :--- | :--- | :--- | :--- |
| 冷启动（首屏绘制） | < 300 ms | `time` + `perf trace` | 是 |
| 打开 1 GiB 文件 | < 500 ms（含索引构建） | `tst_open_file.cpp` | 是 |
| 索引 1 亿行 | < 2 s | `bench_1gb.cpp` | 是 |
| 单条正则全量匹配 1 亿行 | < 3 s（可取消） | `bench_1gb.cpp --regex` | 是 |
| 滚动帧率（100 万行文件的实时滚动） | ≥ 60 fps | `paint()` 内 `perf` 采样 | 是 |
| 标记跳转（1 亿行） | < 1 ms | `tst_marker.cpp` | 是 |
| 内存占用（1 GiB 文件，全文件访问后） | ≤ 1.4 GiB（R-08，典型视口场景 < 350 MB） | `/proc/self/smaps_rollup` | 是 |
| 磁盘占用（.deb） | < 2 MB | `du -b` | 是 |
| AppImage 大小 | < 60 MB | `du -b` | 否 |

**CI 阈值**：超过目标值 2× 即阻断合并。

### 9.2 内存分解（P1 场景：1 GiB 文件，3000 万行）

| 组件 | 大小 | 说明 |
| :--- | ---: | :--- |
| mmap 数据 | 1.00 GiB | 文件映射（虚拟地址空间；Rss 按需换入） |
| `LineMeta` 数组 | 240 MB | 3000 万行 × 8 B |
| `FilterResult.states` | 30 MB | 3000 万行 × 1 B |
| `RuleSet` / `matchCounts` | < 1 MB | 最多 64 条规则（R-03） |
| `MarkerManager` | < 1 MB | 假设 1 万标记 |
| Qt 运行时 | ~30 MB | Qt6 + 字体 + 图标 |
| 系统开销 | ~40 MB | 线程栈、动态库 |
| **虚拟地址合计** | **~1.32 GiB** | — |
| **物理内存（Rss，打开后仅视口访问）** | **~0.35 GiB** | mmap 按需换入 |
| **物理内存（Rss，全文件滚动一遍后）** | **≤ 1.4 GiB** | 最坏情形（R-08，ARCH-UB §13 的 1.1 GiB 未计入 mmap+结构体，修订为 1.4） |

> 注：`mmap` 的页只在被访问时才计入 Rss。视口内约 100 行，实际物理内存占用约 100 MB + Qt 开销。
> 状态栏显示 Rss（物理内存），而非虚拟地址空间。

### 9.3 内存分解（P2 场景：1 亿行，1.5 GiB 文件）

| 组件 | 大小 | 说明 |
| :--- | ---: | :--- |
| mmap 数据 | 1.50 GiB | 文件映射 |
| `LineMeta` 数组 | 800 MB | 1 亿行 × 8 B |
| `FilterResult.states` | 100 MB | 1 亿行 × 1 B |
| `RuleSet` | < 1 MB | — |
| `MarkerManager` | < 4 MB | 假设 10 万标记 |
| Qt 运行时 | ~30 MB | — |
| 系统开销 | ~40 MB | — |
| **虚拟地址合计** | **~2.48 GiB** | — |
| **物理内存（Rss）** | **≤ 1.2 GiB** | 按需换入 |

> P2 场景的 `LineMeta` 占 800 MB，是最大开销。
> 若内存预算不足，v1.1 可启用 `--compact-index` 选项，用 4 字节紧凑表示（`offset` 32 bit + `length` 16 bit），
> 将 `LineMeta` 降至 400 MB。代价是不支持 > 64 KB 的单行。

### 9.4 性能优化清单

| 优化 | 层级 | 预期收益 | 优先级 |
| :--- | :--- | :--- | :--- |
| `memchr` 向量化扫描换行符 | DAL | 索引 6–12× 快 | MUST |
| `MADV_RANDOM` 提示内核 | DAL | 减少预读浪费 | MUST |
| `nice(19)` 降低匹配线程优先级 | BLL | UI 不卡顿 | MUST |
| 双缓冲 `shared_ptr` 快照 | BLL/UI | 零锁渲染 | MUST |
| `ruleColorRef` 替代颜色数组 | BLL | 省 400 MB/亿行 | MUST |
| 固定行高 `setUniformItemSizes` | UI | 滚动 60 fps | MUST |
| 转码 LRU 缓存 | UI | 减少重复转码 | SHOULD |
| 超宽行截断（16384 字符） | UI | 避免单行卡死 | SHOULD |
| 正则预编译（PCRE2 C API） | BLL | 匹配 2–3× 快 | v1.1 |
| SIMD 加速 UTF-8 校验 | DAL | 检测 4–8× 快 | SHOULD |
| 行哈希去重 | BLL | 减少重复渲染 | v1.1 |
| 增量索引（追加文件） | DAL | 避免全量重建 | v1.1 |

---

## 10. 测试策略

### 10.1 测试矩阵

| 层级 | 工具 | 覆盖范围 | CI 阻断 |
| :--- | :--- | :--- | :--- |
| 单元 | `QtTest` + `gtest` | BLL/DAL 100% 行覆盖 | 是 |
| 集成 | `QtTest` + 真实 mmap | 1 MB / 100 MB / 1 GiB | 是 |
| 性能 | 自定义 benchmark | §9.1 全部指标 | 是（阈值 2×） |
| 打包 | `lintian` + `dpkg` | 无 error/warning | 是 |
| 冒烟 | `sudo dpkg -i` + `--version` | 安装后可用 | 是 |
| 静态 | `clang-tidy` / `clang-format` | 0 warning | 否 |
| 沙箱 | `qemu-user`（arm64） | 跨架构编译 | 否 |

### 10.2 单元测试用例

#### 10.2.1 `tst_buffer.cpp`（DAL）

```
用例清单：
  - testEmptyFile: 空文件 → rowCount() == 0, isValid() == true（R-06）
  - testSingleLine: "hello" → rowCount() == 1, length == 5
  - testSingleNewline: "\n" → rowCount() == 1, length == 0（空文件之外的边界）
  - testMultipleLines: "a\nb\nc" → rowCount() == 3
  - testCRLF: "a\r\nb" → rowCount() == 2, 长度正确
  - testCROnly: "a\rb" → rowCount() == 2
  - testNoTrailingNewline: "a\nb" → rowCount() == 2
  - testTrailingNewline: "a\n" → rowCount() == 1（不追加空行）
  - testEmptyLines: "\n\n" → rowCount() == 2, 长度均为 0
  - testBOM: "EF BB BF hello" → 跳过 BOM，内容正确
  - testLargeFile: 100 MB 文件 → 索引时间 < 100 ms
  - testUtf8Strict: 合法 UTF-8 → encoding == Utf8
  - testGbk: GBK 编码文件 → encoding == GBK
  - testCancel: 索引过程中取消 → 返回 Cancelled
  - testLargeFile: 5 GiB 文件 → 返回 UnsupportedFormat
```

#### 10.2.2 `tst_filter.cpp`（BLL）

```
用例清单：
  - testNoRules: 无规则 → 所有行 Normal
  - testExcludeOnly: 仅 Exclude → 命中行 Hidden，其他 Normal
  - testIncludeOnly: 仅 Include → 命中行 Highlighted，其他 Dimmed
  - testIncludeExclude: Include + Exclude → Include 优先（§3.2.2）
  - testSubstring: Substring 模式 → 正确匹配
  - testRegex: Regex 模式 → 正确匹配
  - testCaseSensitive: 大小写敏感 → 正确区分
  - testCaseInsensitive: 大小写不敏感 → 正确忽略
  - testWholeWord: 词边界 → 正确区分 "error" 与 "errorlog"
  - testMultipleRules: 多规则 → 状态决策表正确
  - testCancel: 匹配过程中取消 → 返回 Cancelled
  - testTimeout: 正则超时 → 返回 Timeout
  - testMatchCount: 命中计数正确
  - testRuleRef: 颜色引用正确
  - testFingerprint: 规则指纹稳定
```

#### 10.2.3 `tst_marker.cpp`（BLL）

```
用例清单：
  - testAddRemove: 添加/删除标记
  - testToggle: 切换标记
  - testNextForward: 向前跳转
  - testNextBackward: 向后跳转
  - testWrapForward: 末尾环绕到开头
  - testWrapBackward: 开头环绕到末尾
  - testMultipleMarkers: 多标记独立
  - testDuplicate: 重复添加 → 返回 false
  - testRemoveNonexistent: 删除不存在的标记 → 返回 false
  - testClear: 清空所有标记
  - testSaveLoad: 序列化/反序列化
  - testPerformance: 100 万标记添加/查找 < 10 ms
```

#### 10.2.4 `tst_search.cpp`（BLL）

```
用例清单：
  - testSimpleContains: 简单包含 → 正确找到
  - testCaseSensitive: 大小写敏感 → 正确区分
  - testCaseInsensitive: 大小写不敏感 → 正确忽略
  - testRegex: 正则模式 → 正确匹配
  - testWholeWord: 词边界 → 正确区分
  - testStartLine: 从指定行开始 → 正确跳过
  - testMaxHits: 超出 maxHits → 正确截断
  - testCancel: 搜索过程中取消 → 返回 Cancelled
  - testViewport: 视口搜索 → 正确限制范围
  - testEmptyPattern: 空 pattern → 返回 RegexError
  - testInvalidRegex: 非法正则 → 返回 RegexError
```

### 10.3 集成测试

```
tst_open_file.cpp:
  - 打开 1 MB 文件 → 验证行数、内容、编码
  - 打开 100 MB 文件 → 验证行数、索引时间 < 50 ms
  - 打开 1 GB 文件 → 验证行数、索引时间 < 500 ms
  - 打开 GBK 文件 → 验证转码正确
  - 打开 UTF-16 文件 → 验证转码正确
  - 打开空文件 → 验证不崩溃
  - 打开不存在文件 → 验证返回 FileNotFound
  - 打开无权限文件 → 验证返回 PermissionDenied
```

### 10.4 性能测试

```
bench_1gb.cpp:
  - 生成 1 GiB 测试文件（日志格式）
  - 测量打开时间（含索引）
  - 测量正则匹配时间
  - 测量滚动帧率
  - 测量内存占用
  - 对比阈值（2× 阻断）
```

### 10.5 CMake 集成

```cmake
# tests/CMakeLists.txt
enable_testing()

find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst_buffer unit/tst_buffer.cpp)
target_link_libraries(tst_buffer PRIVATE core Qt6::Test)
add_test(NAME tst_buffer COMMAND tst_buffer)

add_executable(tst_filter unit/tst_filter.cpp)
target_link_libraries(tst_filter PRIVATE core Qt6::Test)
add_test(NAME tst_filter COMMAND tst_filter)

add_executable(tst_marker unit/tst_marker.cpp)
target_link_libraries(tst_marker PRIVATE core Qt6::Test)
add_test(NAME tst_marker COMMAND tst_marker)

add_executable(tst_search unit/tst_search.cpp)
target_link_libraries(tst_search PRIVATE core Qt6::Test)
add_test(NAME tst_search COMMAND tst_search)

add_executable(tst_open_file integration/tst_open_file.cpp)
target_link_libraries(tst_open_file PRIVATE core ui Qt6::Test)
add_test(NAME tst_open_file COMMAND tst_open_file)

add_executable(bench_1gb performance/bench_1gb.cpp)
target_link_libraries(bench_1gb PRIVATE core)
add_test(NAME bench_1gb COMMAND bench_1gb --threshold 2.0)
```

---

## 11. 可观测性

### 11.1 日志级别

| 级别 | 用途 | 输出目标 |
| :--- | :--- | :--- |
| `QtDebugMsg` | 调试信息 | `--verbose` 时输出到 stderr |
| `QtInfoMsg` | 信息（文件打开、过滤完成） | stderr |
| `QtWarningMsg` | 警告（编码降级、正则超时） | stderr + crash.log |
| `QtCriticalMsg` | 严重错误（mmap 失败、OOM） | stderr + crash.log |
| `QtFatalMsg` | 致命错误（崩溃） | stderr + crash.log |

### 11.2 崩溃日志

- 路径：`~/.cache/textanalyst-qt/textanalyst-qt.log`（QStandardPaths::CacheLocation）
- 格式：`yyyy-MM-dd HH:mm:ss.zzz [I|W|C|F] 文件:行号 消息`
- 轮转（R-12 修订）：单档上限 4 MiB，保留 4 个历史档（`.1`–`.4`），
  合计最多 20 MiB；实现为 `io/LogRotator`（写入前预告式换档，
  线程安全，任意线程可调用）。原 10 MiB 单备份方案废止。
- 级别：Info 及以上落盘（文件打开/过滤/导出等关键动作 qInfo 时间线）；
  Debug 只输出 stderr，`--verbose` 控制详细度。
- 上报：默认关闭

### 11.3 性能采样

```
状态栏内存显示：
  - 读取 /proc/self/smaps_rollup 的 "Rss:" 行
  - 每 1 s 更新一次（QTimer）
  - 显示格式："内存: 123 MB / 456 MB (mapped)"

正则耗时显示：
  - FilterEngine::applyParallel 记录耗时
  - 完成后更新状态栏
  - 显示格式："正则: 1234 ms"

滚动帧率（调试模式）：
  - --verbose 时启用
  - LogViewDelegate::paint 内记录耗时
  - 每 1 s 统计平均帧率
  - 显示格式："FPS: 60"
```

### 11.4 信号处理

```cpp
// src/main.cpp
#include <csignal>
#include <QCoreApplication>
#include <QTimer>

static void signalHandler(int sig) {
    // 通过 QTimer::singleShot 调度到主线程
    QTimer::singleShot(0, []() {
        QCoreApplication::quit();
    });
}

static void installSignalHandlers() {
    struct sigaction sa{};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP,  &sa, nullptr);
}
```

**MUST**：信号处理函数中不得调用 Qt API（非线程安全），只通过 `QTimer::singleShot` 调度。

### 11.5 调试环境变量

```bash
QT_LOGGING_RULES="*.*=true"          # 全量 Qt 日志
QT_FATAL_WARNINGS=1                   # 警告视为致命
QT_SCALE_FACTOR=1                     # HiDPI 调试
QT_QPA_PLATFORM=xcb                   # 强制 X11（Wayland 调试）
LANG=zh_CN.UTF-8                      # 中文界面
QT_IM_MODULE=fcitx                    # fcitx 输入法
```

---

## 12. 代码规范

### 12.1 命名约定

| 类别 | 约定 | 示例 |
| :--- | :--- | :--- |
| 命名空间 | 小写 | `namespace tat` |
| 类 | PascalCase | `FilterEngine` |
| 结构体 | PascalCase | `FilterRule` |
| 枚举 | PascalCase（成员 PascalCase） | `enum class ResultState { Normal, Hidden }` |
| 方法 | camelCase | `applyParallel()` |
| 成员变量 | 前缀 `m_` | `m_buffer` |
| 常量 | 前缀 `k` + PascalCase | `kMarkerCount` |
| 类型别名 | PascalCase | `using LineNo = int` |
| 宏 | 全大写 + 下划线 | `TAT_DEBUG_LOG` |

### 12.2 文件格式

```
// src/core/engine/FilterEngine.h
#pragma once                          // 头文件保护
#include <cstdint>                    // 标准库
#include "FilterRule.h"              // 项目头文件

namespace tat {                      // 命名空间

class FilterEngine {                 // 类声明
public:                              // 公开接口
    // 方法注释：说明用途、参数、返回值、线程安全
    static Error apply(...);

private:                             // 私有实现
    // 私有成员变量
    int m_member;
};

} // namespace tat                   // 命名空间结束
```

### 12.3 注释规范

```
// 类注释：说明职责、线程安全、使用场景
// 方法注释：说明用途、参数、返回值、前置/后置条件、线程安全
// 关键算法：说明复杂度、边界条件、已知限制
// 决策注释：说明"为什么这样做"，引用 ADR 或冲突裁决
```

**MUST**：公共接口必须有注释，私有方法按复杂度决定。

### 12.4 错误处理

```cpp
// 返回 Error 而非抛异常
Error doSomething(...) {
    if (!valid) return Error{ErrCode::InvalidArgument, "op", "message"};
    // ...
    return Error::none();
}

// 调用方检查错误
Error err = doSomething(...);
if (err.hasError()) {
    qWarning() << "Failed:" << err.message.c_str();
    return err;  // 或转为用户可见反馈
}
```

### 12.5 线程安全标注

```cpp
// 方法注释中标注线程安全
// [M] 仅主线程调用
// [W] 仅工作线程调用
// [A] 任意线程可调用
// [LS] 锁保护（说明锁名）
```

### 12.6 性能标注

```cpp
// 方法注释中标注性能特征
// 复杂度：O(log n)
// 预期耗时：< 1 ms（1 亿行）
// 内存：O(1)
```

---

## 13. 已知限制与 v1.1 计划

### 13.1 v1.0 已知限制

| 编号 | 限制 | 影响 | v1.1 计划 |
| :--- | :--- | :--- | :--- |
| L1 | 正则预编译实际是"预构造"，非"预编译" | P2 场景匹配慢 2–3× | 改用 PCRE2 C API |
| L2 | 非 UTF-8 编码的搜索高亮可能错位 | 视觉错位 1–2 px | 字符偏移支持 |
| L3 | `MADV_DONTNEED` 默认禁用 | 内存压力下无法自动卸载 | 完善受控卸载协议 |
| L4 | 单文件 > 4 GiB 不支持完整功能 | 降级到分段模式（接口层面） | 完整实现 SegmentedTextBuffer |
| L5 | 单行 > 1 MB 截断显示 | 超长行无法完整查看 | 行内分页 |
| L6 | LRU 缓存用清空式淘汰 | 滚动时可能频繁重算 | 改用 QCache 或自实现 LRU |
| L7 | 标记不持久化到 .tat | 仅存会话 | 支持 .tat 中标记 |
| L8 | 不支持多文件标签页 | 只能打开一个文件 | 多标签页支持 |
| L9 | 不支持文本替换 | 仅搜索 | 替换功能 |
| L10 | 不支持插件接口 | 无法扩展 | 插件架构 |

### 13.2 v1.1 优先事项

1. **正则预编译**（L1）：改用 PCRE2 C API，消除 Qt 正则开销。
2. **字符偏移**（L2）：支持非 UTF-8 编码的精确高亮。
3. **完整分段索引**（L4）：支持 > 4 GiB 单文件。
4. **行内分页**（L5）：超长行分页显示。
5. **多标签页**（L8）：同时打开多个文件。

### 13.3 v1.2 展望

- 插件接口（正则引擎、渲染主题、导出格式）
- 增量索引（追加文件无需全量重建）
- 远程文件支持（SFTP、HTTP）
- GPU 加速渲染（OpenGL/Qt Quick）
- 协作标注（WebSocket 同步标记）

---

## 14. 与 ARCH-UB 的一致性核对

### 14.1 已覆盖的 ARCH-UB 章节

| ARCH-UB 章节 | 本文档章节 | 覆盖状态 |
| :--- | :--- | :--- |
| §0 文档使用约定 | §0.5 | ✅ 一致 |
| §1 环境基线矩阵 | §0.3 | ✅ 一致 |
| §2 总体分层架构 | §0.3 | ✅ 一致 |
| §2.1 依赖倒置表 | §4.2, §5.2 | ✅ 细化 |
| §3 DAL | §2 | ✅ 细化 |
| §3.1 MemoryMappedFile | §2.2 | ✅ 一致 |
| §3.2 LineIndexer | §2.4 | ✅ 细化 |
| §3.3 编码检测 | §2.3 | ✅ 细化 |
| §3.4 inotify | §2.6 | ✅ 一致 |
| §4 BLL | §3 | ✅ 细化 |
| §4.1 FilterEngine | §3.2 | ✅ 细化 |
| §4.2 MarkerManager | §3.4 | ✅ 修正（unordered_set → vector） |
| §4.3 Searcher | §3.5 | ✅ 细化 |
| §5 UI | §7 | ✅ 细化 |
| §5.1 LogListView | §7.4 | ✅ 一致 |
| §5.2 桌面集成 | §7.6 | ✅ 覆盖 |
| §5.3 布局 | §7.6.1 | ✅ 一致 |
| §5.4 防抖 | §7.5.2 | ✅ 一致 |
| §6 Controller | §6 | ✅ 细化 |
| §6.1 MainController | §6.2 | ✅ 细化 |
| §6.2 命令行 | §6.5 | ✅ 一致 |
| §6.3 信号处理 | §11.4 | ✅ 一致 |
| §7 持久化 | §6.4, §11 | ✅ 覆盖 |
| §7.1 XDG 目录 | §0.3 | ✅ 一致 |
| §7.2 .tat 格式 | §6.2 | ✅ 覆盖 |
| §7.3 QSettings | §6.4 | ✅ 一致 |
| §8 并发模型 | §5 | ✅ 细化 |
| §9 目录结构 | — | 由 CMakeLists 维护 |
| §10 构建系统 | — | 由 CMakeLists 维护 |
| §11 打包部署 | — | 由 packaging/ 维护 |
| §12 系统集成 | — | 由 .desktop 文件维护 |
| §13 性能预算 | §9 | ✅ 细化 |
| §14 安全与稳定性 | §8, §11 | ✅ 覆盖 |
| §15 测试策略 | §10 | ✅ 细化 |
| §16 里程碑 | — | 由项目管理维护 |
| §17 ADR | §0.4 | ✅ 覆盖 |
| §18 风险登记册 | §13 | ✅ 覆盖 |
| §19 附录 | — | 由 docs/BUILD.md 维护 |

### 14.2 已解决的冲突

见 §0.4 冲突裁决表（10 项冲突，全部已决策）。

### 14.3 新增设计决策

| 编号 | 决策 | 理由 |
| :--- | :--- | :--- |
| D1 | `LineMeta` 固定 8 字节，不含 hash | 内存预算（§1.3） |
| D2 | `RuleSet` 不可变快照 | 并发安全（§3.2.3） |
| D3 | `shared_ptr` 快照替代裸指针双缓冲 | 内存安全（§5.4） |
| D4 | `CompileFn` 注入替代 Qt 正则成员 | BLL 零 Qt 依赖（§4.1） |
| D5 | `ThreadPool` 抽象替代直接 `QThreadPool` | BLL 零 Qt 依赖（§4.2） |
| D6 | 错误码返回替代 C++ 异常 | 线程安全（§1.4） |
| D7 | 状态决策表（8 场景） | 消除歧义（§3.2.2） |
| D8 | `ruleColorRef` 替代颜色数组 | 内存节省 80%（§3.3） |
| D9 | 隐藏行保留行槽 | 行号对齐（§7.3.5） |
| D10 | `MADV_DONTNEED` 默认禁用 | 渲染不变式（§2.2.4） |
| D11 | `SharedSnapshot<T>` 替代 `std::atomic<std::shared_ptr<T>>` | C++17 兼容（R-01） |
| D12 | 同时启用规则上限 64（`kMaxRules`） | `ruleRef` 6 bit（R-03） |
| D13 | 空文件语义：`rowCount()==0`，`"\n"` 文件 `rowCount()==1` | 边界一致（R-06） |
| D14 | 内存预算修订为 ≤1.4 GiB（全文件访问最坏情形） | P1 场景分解复核（R-08） |

---

## 15. 附录

### 15.1 文件头注释模板

```cpp
// src/core/engine/FilterEngine.h
// =============================================================================
// FilterEngine: 过滤规则执行引擎
//
// 职责：
//   - 分片并发执行过滤规则
//   - 状态决策（§3.2.2）
//   - 取消协议（§3.2.4）
//   - 正则超时保护（§3.2.6）
//
// 线程安全：
//   - apply/applyParallel: [W] 工作线程调用
//   - classifyLine: [A] 任意线程
//   - fingerprint/ruleFromLine: [A] 任意线程
//
// 性能：
//   - applyParallel: O(N/W) 每行，W = 线程数
//   - 1 亿行 / 8 线程: < 3 s（含正则）
//
// 已知限制：
//   - 正则预编译实际是"预构造"（§13.1 L1）
// =============================================================================
#pragma once
```

### 15.2 类图（简化）

```
┌─────────────────┐
│   MainWindow    │
└────────┬────────┘
         │ 信号槽
         ▼
┌─────────────────┐     ┌──────────────────┐
│ MainController  │────▶│ ResultStore      │
│                 │     │ (atomic<shared>)  │
│ - m_buffer      │     └──────────────────┘
│ - m_resultStore │
│ - m_markers     │     ┌──────────────────┐
│ - m_token       │────▶│ TextBuffer       │
└────────┬────────┘     │ (shared_ptr)     │
         │              └────────┬─────────┘
         │ 调用                  │ 持有
         ▼                      ▼
┌─────────────────┐     ┌──────────────────┐
│ FilterEngine    │     │ MemoryMappedFile │
│ (静态方法)      │     │ (mmap)           │
└────────┬────────┘     └──────────────────┘
         │ 使用
         ▼
┌─────────────────┐
│ RuleSet         │
│ (不可变快照)    │
└─────────────────┘

┌─────────────────┐     ┌──────────────────┐
│ LogViewDelegate │────▶│ MainController   │
│                 │     │ (bufferSnapshot) │
│ - m_transcode   │     └──────────────────┘
│ - m_searchHits  │
└─────────────────┘
```

### 15.3 时序图（文件打开）

```
MainWindow    MainController    TextBuffer    MemoryMappedFile   LineIndexer    EncodingDetector
    │              │                │               │                │                │
    │ openFile()   │                │               │                │                │
    ├─────────────▶│                │               │                │                │
    │              │ create()       │               │                │                │
    │              ├───────────────▶│               │                │                │
    │              │                │ open()        │                │                │
    │              │                ├──────────────▶│                │                │
    │              │                │               │ mmap()         │                │
    │              │                │               ├───────────┐    │                │
    │              │                │               │◀──────────┘    │                │
    │              │                │◀──────────────┤                │                │
    │              │                │ detect()      │                │                │
    │              │                ├───────────────┼────────────────┼───────────────▶│
    │              │                │◀──────────────┼────────────────┼────────────────┤
    │              │                │ build()       │                │                │
    │              │                ├───────────────┼───────────────▶│                │
    │              │                │               │                │ scan lines     │
    │              │                │               │                │                │
    │              │◀───────────────┤               │                │                │
    │◀─────────────┤                │               │                │                │
    │ fileOpened   │                │               │                │                │
    │              │ bufferChanged  │               │                │                │
    │◀─────────────┤                │               │                │                │
    │ resetModel() │                │               │                │                │
```

### 15.4 时序图（过滤应用）

```
FilterDock   MainController   FilterEngine   QThreadPool    ResultStore   LogViewDelegate
    │            │                │              │              │               │
    │ rulesApplied│               │              │              │               │
    ├───────────▶│                │              │              │               │
    │            │ applyFilter()  │              │              │               │
    │            ├───────────────▶│              │              │               │
    │            │ cancel old     │              │              │               │
    │            ├──────────────────────────────▶│              │               │
    │            │ submit new     │              │              │               │
    │            ├──────────────────────────────▶│              │               │
    │            │                │ applyParallel│              │               │
    │            │                ├─────────────▶│              │               │
    │            │                │              │ chunk tasks  │               │
    │            │                │              ├──────────┐   │               │
    │            │                │              │◀─────────┘   │               │
    │            │                │◀─────────────┤              │               │
    │            │◀───────────────┤              │              │               │
    │            │ publish        │              │              │               │
    │            ├──────────────────────────────────────────────▶│               │
    │            │                │              │              │               │
    │            │                │              │              │ paint()       │
    │            │                │              │              ├──────────────▶│
    │            │                │              │              │ snapshot()    │
    │            │                │              │              │◀──────────────┤
    │            │                │              │              │               │
```

### 15.5 术语表（补充）

| 术语 | 含义 |
| :--- | :--- |
| P1 / P2 | 性能场景：大文件（1 GiB）/ 多行文件（1 亿行） |
| LRU | 最近最少使用（缓存淘汰策略） |
| FNV-1a | 快速非加密哈希（§1.3.2） |
| MADV_RANDOM | `madvise` 提示：随机访问模式 |
| MADV_DONTNEED | `madvise` 提示：释放已映射页 |
| PCRE2 | Perl-Compatible Regular Expressions v2 |
| `Token` | 取消令牌（世代号机制） |
| 快照（Snapshot） | `SharedSnapshot<T>::snapshot()` 取得的私有 `shared_ptr<T>` |
| `ruleColorRef` | 6 bit 索引，指向 `RuleSet.rules` 的颜色 |

---

> **文档结束（v1.1 审核修订版）**
>
> 下一步（开发基线）：
> - 仓库目录结构按 §ARCH-UB 9 + 本设计的接口清单落地，CMake 工程 + tests 骨架。
> - M1：DAL（MemoryMappedFile / EncodingDetector / LineIndexer / TextBuffer）+ `tst_buffer.cpp`。
> - M2：BLL（FilterEngine / MarkerManager / Searcher / ResultStore）+ `tst_filter/marker/search.cpp`。
> - M3：IO/Controller（TatSerializer / SettingsManager / MainController / CLI）+ 集成测试。
> - M4：UI（LogListView / Delegate / Dock / MainWindow），在具备 Qt6 的环境编译。
>
> 评审重点：§0.4 冲突裁决、§3.2.2 状态决策表、§3.4 MarkerManager 容器选择、
> §5.4 双缓冲设计（SharedSnapshot）、§0.4 末尾的 v1.1 修订记录（R-01..R-11）。





