# TextAnalyst-Qt 系统架构文档（Ubuntu 专版）

> 版本：v1.0.1-ubuntu（v1.0 审核修订版，2026-08-29）
> 适用范围：**仅在 Ubuntu（20.04 / 22.04 / 24.04 LTS）桌面环境**中开发与运行
> 上游参考：`docs/architecture/01-blueprint.md`（v1.0 蓝皮书）
> 详细设计：`docs/architecture/03-detailed-design.md`（v1.1）——接口/算法级细化，本文档为约束级
> 目标读者：开发工程师、构建工程师、发布/打包工程师、QA

本文档在 `docs/architecture/01-blueprint.md` 总体设计的基础上，把所有横切决策收敛到 **Ubuntu 单一运行平台**：
统一包管理与编译器、统一路径规范、统一打包格式、统一系统集成方式。任何"跨平台"
选项（Windows API、macOS 专用路径、通用注册表抽象等）在本项目内**不予实现**，避免
引入无效抽象与测试矩阵。

---

## 0. 文档使用约定

| 标记 | 含义 |
| :-- | :-- |
| **MUST** | 项目强制约定，违反即代码评审退回 |
| **SHOULD** | 强烈推荐；偏离需在 PR 中说明 |
| **MAY** | 可选，按场景判断 |
| **N/A (Ubuntu)** | 原蓝皮书中的跨平台抽象，本项目**不实现** |

所有路径均遵循 **XDG Base Directory Specification**；所有时间戳为
`QDateTime`（内部 UTC，展示使用系统时区）。

---

## 1. 环境基线矩阵

### 1.1 支持的 Ubuntu 版本

| Ubuntu 版本 | 代号 | 内核 | glibc | Qt (apt) | GCC | 支持级别 | 备注 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 20.04 LTS | Focal | 5.4   | 2.31 | 5.12.8 / 5.15.13（PPA） | 9.4  | **维护态** | 2025-05 后仅安全更新 |
| 22.04 LTS | Jammy | 5.15  | 2.35 | 5.15.13 / 6.4.2 | 11.4 | **稳定支持** | CI 主基线 |
| 24.04 LTS | Noble | 6.8   | 2.39 | 5.15.13 / 6.4.2 | 13.3 | **稳定支持（开发主环境）** | CI 主基线 |

- **MUST**：CI 在 `ubuntu-22.04` 与 `ubuntu-24.04` 两个 runner 上跑通全部
  单元/集成/打包测试。
- **MAY**：为 20.04 保留编译通过但不保证新特性，仅在其 EOL 前提供 LTS 分支。
- **N/A (Ubuntu)**：不支持 32 位架构（`i386`）、不支持 `ppc64el`/`s390x`；仅支持
  `amd64` 与 `arm64`（`arm64` 仅当用户提供硬件时在 CI 矩阵中启用）。

### 1.2 Qt 版本策略

- **MUST**：目标 Qt **6.4+**（apt `qt6-base-dev`），这是 Ubuntu 22.04/24.04 的默认。
- **SHOULD**：CMake 最低要求 `find_package(Qt6 6.4 REQUIRED ...)`；若需下探到 Qt 5，
  必须显式通过 `-DCMAKE_PREFIX_PATH=/usr/lib/qt5` 并加 `QT5_COMPAT` 宏分支。
- **MAY**：为 20.04 提供 Qt 5.15 PPA 构建（`ppa:beineri/opt-qt`），但**不允许**在
  主分支上出现双 Qt 兼容代码，兼容代码仅存在于 `qt5-compat` 分支。
- **N/A (Ubuntu)**：不构建 macOS SDK，不使用 `Q_OS_WIN` 分支，不引入跨平台 `QRegExp`
  的 Windows-only 后端。

### 1.3 编译器与工具链

| 组件 | Ubuntu 22.04 | Ubuntu 24.04 | 说明 |
| :--- | :--- | :--- | :--- |
| 编译器 | `gcc-11` / `g++-11` | `gcc-13` / `g++-13` | C++17（MUST） |
| CMake | ≥ 3.22 | ≥ 3.28 | `CMAKE_CXX_STANDARD 17` |
| Ninja | ≥ 1.10 | ≥ 1.11 | 首选生成器 |
| Qt 工具 | `moc`/`uic`/`rcc` | 同 | 通过 `qt_add_resources` 调用 |
| 静态检查 | `clang-tidy`（14+） | 同 | CI 非阻塞 |
| 格式化 | `clang-format`（LLVM 16） | 同 | 仓库根 `.clang-format` |

**MUST**：仓库根提供 `.clang-format` 与 `.clang-tidy`；`build.sh` 中显式
`cmake -G Ninja`。

---

## 2. 总体分层架构（Ubuntu 收敛版）

沿用蓝皮书 4 层结构，依赖方向严格自上而下，禁止反向引用：

```
┌─────────────────────────────────────────────────────────────────┐
│ ① Presentation Layer (Qt Widgets / Wayland & X11)              │
│    LogListView / Delegate / FilterDockWidget / MainWindow       │
├─────────────────────────────────────────────────────────────────┤
│ ② Controller / ViewModel Layer (Mediator)                       │
│    MainController · DebounceTimer · CommandLineParser          │
├─────────────────────────────────────────────────────────────────┤
│ ③ Business Logic Layer (Qt-independent, C++17 + STL)           │
│    FilterEngine · MarkerManager · Searcher                      │
├─────────────────────────────────────────────────────────────────┤
│ ④ Data Access Layer (POSIX mmap + fcntl + inotify)             │
│    MemoryMappedFile · LineIndexer · EncodingDetector · FileWatcher │
└─────────────────────────────────────────────────────────────────┘
```

> 注（v1.0.1）：`EncodingDetector` 从第 ③ 层移入第 ④ 层——编码检测处理的是原始字节流，
> 属于数据访问职责；第 ③ 层（BLL）保持"规则/标记/搜索"三个纯计算模块，见 DISPLAYDESIGN §2.3。

**关键约束**：
- ③ 不 `#include <QtGui/QColor>` 之外的任何 Qt 类型；**MUST** 把"颜色"抽象为
  `#RGB` 位字段或 `QColor` 转 `uint32_t` 传入。
- ④ 只依赖 POSIX 头（`sys/mman.h`、`fcntl.h`、`sys/inotify.h`），**禁止**使用
  Windows-only 或 macOS-only API。
- ② 通过 Qt 信号槽跨层通信，**禁止**在 ③/④ 内直接引用 `QObject`。

### 2.1 依赖倒置表

| 消费者 | 提供者 | 通信机制 | Ubuntu 说明 |
| :--- | :--- | :--- | :--- |
| MainWindow | MainController | 信号槽 | 主线程 |
| MainController | FilterEngine | 直接函数调用 + 回调 | 线程安全由 BLL 内部保证 |
| FilterEngine | ThreadPool 抽象 | 注入任务提交（`std::function<void()>`） | 默认线程数 = `QThread::idealThreadCount()`（Ubuntu 上等同 `nproc`）；v1.0.1：BLL 不直接引用 `QThreadPool`，由 UI 层适配，见 DISPLAYDESIGN §4.2 |
| LogViewDelegate | TextBuffer / FrontBuffer | 无锁读 | 双缓冲原子交换 |
| MemoryMappedFile | `mmap`/`munmap` | POSIX syscall | 直接系统调用，不经 `QFile::map` |

---

## 3. 数据访问层（DAL）—— Ubuntu/POSIX 版

### 3.1 `MemoryMappedFile`（POSIX mmap 直用）

**MUST** 使用原生 `mmap()`，不使用 `QFile::map()`（后者在 64 位下 2GB 限制问题
已规避但仍引入不必要的 `QFile` 生命周期耦合）。

```cpp
class MemoryMappedFile {
public:
    static MemoryMappedFile open(const QString& path, MemoryMapFlags flags = ReadOnly);
    const char* data() const noexcept { return m_data; }
    size_t size() const noexcept { return m_size; }   // v1.0.1：统一 size_t，不再混用 qulonglong/off_t
    bool isValid() const noexcept { return m_data != nullptr; }

    // Ubuntu 内存压力响应：主动归还页（v1.0.1：原 MmapAdvise 枚举未定义，改为具体方法）
    bool adviseRandom() noexcept;                     // MADV_RANDOM
    bool adviseSequential() noexcept;                 // MADV_SEQUENTIAL
    bool adviseDontNeed(const char* range, size_t len) noexcept;  // 实验特性

private:
    void*   m_data = nullptr;
    size_t  m_size = 0;
    int     m_fd   = -1;
    bool    m_writable = false;
    uint64_t m_mtime_ns = 0;   // stat() 采样，用于变更检测
};
```

**Ubuntu 关键实现点**：

1. **`fcntl` 读锁**（F_SETLK，非阻塞）：写入时提示"文件被外部占用"，但**不强制
   失败**，与原版 TAT 快照策略一致。
2. **`MADV_RANDOM`**：日志文件随机访问模式提示内核，避免预读浪费。
3. **`MADV_DONTNEED` 惰性卸载**：当 `QSystemInformation::operatingMemorySize()`
   剩余 < 15% 时，对已浏览完毕的段执行 `madvise(MADV_DONTNEED)`；下次访问由
   内核按需换入。**MUST** 记录"已卸载段区间"以在渲染时触发重新加载。
4. **稀疏文件**：写入模式使用 `ftruncate()` 预分配，配合 `posix_fallocate()`
   落盘（ext4 场景下避免写时分配抖动）。
5. **`/proc` 观测**：暴露 `resident_set_kb`（从 `/proc/self/smaps_rollup` 读取）
   到状态栏，方便 QA 验证内存预算。

### 3.2 `LineIndexer`（后台线程偏移量索引）

沿用蓝皮书"延迟解析 + 偏移量索引"策略，数据结构：

```cpp
struct LineMeta {
    uint32_t offset;    // 相对 mmap 基址的字节偏移
    uint32_t length;    // 不含换行符
    uint32_t hash;      // FNV-1a 前 32 位（可选，用于行去重；v1.1 按需构建）
};
class TextBuffer {
public:
    const LineMeta* meta(int i) const noexcept;
    int rowCount() const noexcept;
    // v1.0.1：std::span 是 C++20 特性（libstdc++ 在 -std=c++17 下不可用），
    // 改为指针 + 计数，语义等价。
    const LineMeta* lineData() const noexcept;
    size_t         lineCount() const noexcept;
    const char* basePtr() const noexcept;
private:
    std::vector<LineMeta> m_lines;
    const char* m_basePtr = nullptr;
    QAtomicInt  m_dirty;
};
```

**Ubuntu 关键实现点**：

- 索引构建任务通过 `QtConcurrent::run` 提交到 `QThreadPool`，线程数上限 =
  `QThread::idealThreadCount()`，**MUST** 使用 `std::atomic<size_t>` 计数进度。
- 行尾识别顺序：`\r\n` → `\n` → `\r`（Windows 日志在 Ubuntu 下很常见，
  必须正确识别 `\r\n` 而不是把 `\r` 当行尾）。
- 超长行（> 4GB 不可能，但 > 1GB 单行）：`uint32_t` 偏移溢出防护 —— 若检测到，
  降级到"分段 buffer"策略，并弹窗提示"检测到超长行"。

### 3.3 编码检测（Ubuntu 场景）

| 优先级 | 检测手段 | 说明 |
| :--- | :--- | :--- |
| 1 | BOM (`EF BB BF` / `FF FE` / `FE FF` / `FF FE 00 00`) | 直接采信 |
| 2 | `iconv` 试转 UTF-8 严格校验 | Ubuntu 自带 `libc`，`iconv` 稳定 |
| 3 | ICU `ucnv_toUnicode` 探测 | Ubuntu 24.04 提供 `libicu-dev` |
| 4 | 系统 locale 兜底 `QByteArray::fromLocal8Bit()` | 使用 `LC_ALL` / `LANG` |

**MUST**：索引层始终以 **UTF-8 字节偏移**存储；ANSI/GB18030 等仅**在渲染时**
转换，避免"索引漂移"。

### 3.4 文件变更监听（`inotify`）

- 对当前文件监听 `IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB`。
- 触发后**不自动重载**，仅显示状态栏提示"文件已被外部修改，点击重载"。
- 监听 fd 数量受 `/proc/sys/fs/inotify/max_user_watches` 限制（Ubuntu 默认
  65536）；**MUST** 每次仅监听 1 个文件（当前文档），不做多文件监听。

---

## 4. 业务逻辑层（BLL）

### 4.1 `FilterEngine`（分片 + 双缓冲）

沿用蓝皮书设计，Ubuntu 补充：

- **分片粒度**：`chunkSize = max(1024, totalLines / (idealThreadCount * 8))`；
  避免小任务（< 1ms）造成线程调度开销超过计算。
- **线程优先级**：后台匹配线程调用 `nice(19)` 降至最低（Ubuntu 下 root 也生效）；
  UI 主线程保持 `nice(0)`。
- **CPU 亲和性**：**MAY** 通过 `pthread_setaffinity_np()` 将匹配线程绑定到
  `idealThreadCount` 个 CPU，避免在容器/虚拟机中触发调度抖动。
- **双缓冲交换**：使用 `SharedSnapshot<const FilterResult>` 原子发布（v1.0.1 修订：
  基于 `std::atomic_load/store` 自由函数的 `shared_ptr` 快照，C++17 兼容；
  详见 DISPLAYDESIGN §1.5 / §5.4）。旧 buffer 由引用计数自然释放——
  最后一个读者析构时才真正释放，避免裸指针"延后 delete"导致的 use-after-free。
- **取消协议**：每个 chunk 内部每 8KB 检查一次 `QAtomicInt m_cancel`；用户修改
  规则时 `m_cancel = 1` 并 `QThreadPool::waitForDone()` 超时 500ms 后放弃旧任务。

### 4.2 `MarkerManager`（8 标记）

- 存储：`std::array<std::vector<int>, 8>`，每个 vector 按行号**严格升序**
  （v1.0.1 修订：原 `unordered_set` 方案迭代顺序无序，`std::next(find(x))` 不是
  "下一个更大行号"，无法实现循环跳转语义；有序 vector 的添加/删除/跳转均为
  O(log n)，满足"1 亿行内标记跳转 < 1 ms"，见 DISPLAYDESIGN §3.4）。
- 循环跳转：`std::upper_bound(v.begin(), v.end(), line)`，到达 `end()` 则环绕到 `begin()`。
- 快捷键：`Ctrl+1..8` 切换当前行的标记；`Alt+1..8` 跳转到下一处。

### 4.3 `Searcher`（独立于过滤器）

- 默认 `QRegularExpression`（Qt 6 内置 PCRE2，Ubuntu 24.04 系统包
  `libpcre2-8-0` 已装）。
- 提供 "简单包含" 走 `QByteArray::contains()` 快速路径（对 ASCII 纯日志场景
  比正则快 5–10×）。
- **MUST** 显式拒绝空 pattern（v1.0.1 修订：Qt 6.4 无
  `QRegularExpression::DontMatchEmptyString` 枚举，等效语义由空模式校验实现），
  并按需加 `QRegularExpression::CaseInsensitiveOption`。
- **防 DoS**：单个匹配耗时 > 3s 时弹窗提示"正则可能过慢，是否停止？"，
  提供"停止匹配"按钮（调用 4.1 的取消协议）。

---

## 5. UI 表现层（Qt Widgets）

### 5.1 `LogListView`（`QAbstractItemView` 虚拟化）

- 模型：`QAbstractListModel`，`rowCount()` 返回 `TextBuffer::rowCount()`，
  `data()` 仅按行号取 `LineMeta` 偏移量 → `QString::fromUtf8(mmap 段)`。
- 委托：`LogViewDelegate : public QStyledItemDelegate`，`paint()` 只做
  **查表 + 内存拷贝**，绝不进行正则。
- **MUST** `canFetchMore` / `fetchMore` 返回默认实现（不启用 lazy fetch），
  避免 Qt 内部异步预取与我们的双缓冲冲突。

### 5.2 Ubuntu 桌面集成

| 关注点 | 决策 |
| :--- | :--- |
| 主题 | 默认 `Fusion`；跟随系统 `QStyleFactory::create("Fusion")` |
| 显示协议 | Qt 6 自动适配 Wayland / X11（Ubuntu 24.04 默认 Wayland） |
| HiDPI | `QT_SCALE_FACTOR_ROUNDING_POLICY=PassThrough`；图标使用 SVG（`resources/icons/*.svg`） |
| 输入法 | 依赖 Ubuntu 自带 `fcitx5-qt6` / `ibus-qt6`（apt 已安装） |
| 中文字体 | `Noto Sans CJK SC`（`fonts-noto-cjk` 包）；MUST 在打包脚本中将其加入 Suggests |
| 字体回退 | `QFontDatabase::systemFont(QFontDatabase::FixedFont)` 获取等宽字体 |
| 剪贴板 | 使用 `QClipboard::systemClipboard()`（Wayland 下走 wl-data-device） |
| 拖放 | `QDragEnterEvent` 检测 `text/plain` MIME |

### 5.3 布局（Docking）

- 中央：`LogListView`（上）
- 底部 `QDockWidget`：过滤器面板（下）——v1.2 修订 R-16：与日志视图构成
  上下两部分；原版 TAT 三行布局（Filter/颜色下拉 + Text + Description +
  三复选框），View 菜单提供 toggle 恢复；规则表格显示颜色/备注/命中计数
- 顶部工具条：打开 / 保存 / 关闭 / 搜索 / 过滤
- 底部状态栏：文件路径、行数、当前行、内存占用、正则耗时
- **MUST** 保存窗口几何到 `QSettings`（`geometry` 二进制）

### 5.4 输入防抖

- `QLineEdit::textChanged` → `QTimer::singleShot(200, this, [...])`，**MUST**
  使用 `QTimer` 而不是自定义 debounce 类（Ubuntu 下无需 `setTimeout` 兼容层）。
- 防抖窗口可在偏好设置中调节 50–1000ms。

---

## 6. 控制层（Controller）

### 6.1 `MainController`

- 单例或主窗口成员；持有 `TextBuffer` / `FilterEngine` / `MarkerManager`。
- 关键命令方法（**MUST** 通过 Qt 信号对外暴露，避免跨层直接调用）：

| 命令 | 说明 |
| :--- | :--- |
| `openFile(path)` | 打开文件，触发索引构建 |
| `applyFilter(rules)` | 提交过滤任务（带取消协议） |
| `jumpToLine(n)` | 跳转指定行 |
| `toggleMarker(n, line)` | 切换标记 |
| `saveFilters(path)` / `loadFilters(path)` | .tat 序列化 |
| `exportSelection()` | 导出选中行到剪贴板/文件 |

### 6.2 命令行解析（Ubuntu 惯例）

```
textanalyst-qt [options] [file]

Options:
  -h, --help                  Show this help
  -V, --version               Show version
  --file <path>               Open file (overrides positional)
  --line <n>                  Jump to line n
  --grep <pattern>            Open file and filter by pattern
  --load <file.tat>           Load filter rules from .tat
  --export <file>             Export matched lines to file
  --no-restore                Skip last-session restore
  --verbose                   Verbose logging
```

**MUST** 使用 `QCommandLineParser`（Qt 6 自带），与 GNU getopt 长选项风格一致。

### 6.3 信号处理（优雅退出）

- 安装 `sigaction(SIGINT/SIGTERM/SIGHUP)` 处理函数（v1.0.1 修订：信号处理器
  必须在 async-signal-safe 约束下运行，**不得**调用 `QCoreApplication::sendPostedEvent`
  等 Qt API）。采用"自管道"模式：信号处理器内只做 `write()` 到预先创建的
  `socketpair` 写端；主线程用 `QSocketNotifier` 监听读端，可读时调用
  `QCoreApplication::quit()` → `MainWindow::closeEvent()`（见 DISPLAYDESIGN §11.4）。
- **MUST** 保存会话（窗口几何、最近文件、过滤器）到 `QSettings` 后再退出。
- 捕获 `QSystemTrayIcon`（可选）实现后台最小化；Ubuntu 默认启用 tray icon
  通过 `libappindicator`（如需可 apt 依赖 `libayatana-appindicator-dev`）。

---

## 7. 持久化与配置（XDG 规范）

### 7.1 目录约定

| 用途 | 路径 | 说明 |
| :--- | :--- | :--- |
| 用户配置 | `~/.config/textanalyst-qt/` | `QSettings` 自动写入 |
| 用户数据 | `~/.local/share/textanalyst-qt/` | 过滤器 `.tat` 库 |
| 缓存 | `~/.cache/textanalyst-qt/` | 行索引缓存、正则编译缓存 |
| 运行时 | `/run/user/<uid>/textanalyst-qt/` | 临时锁文件、PID |
| 应用图标 | `/usr/share/icons/hicolor/<size>/apps/textanalyst-qt.png` | 打包安装 |
| 桌面项 | `/usr/share/applications/textanalyst-qt.desktop` | 打包安装 |
| MIME | `/usr/share/mime/packages/textanalyst-qt.xml` | 打包安装 |

**MUST** 使用 `QStandardPaths` 获取上述路径，**禁止**硬编码 `~/.config`。

### 7.2 `.tat` 文件格式（与原版 TAT 兼容）

```xml
<?xml version="1.0" encoding="UTF-8"?>
<filters>
  <filter id="1"
          foreColor="#000000"
          backColor="#FFFF00"
          pattern="ERROR"
          isInclude="1"
          matchMode="Substring"
          caseSensitive="0"
          isEnabled="1"/>
</filters>
```

- 序列化：`QXmlStreamWriter`（格式化缩进）；反序列化：`QXmlStreamReader`（流式，
  不加载 DOM）。
- **MUST** 写入前先写临时文件 `*.tat.tmp`，成功后 `rename()` 原子替换；同时
  保留 `*.bak`（上一版）。
- **注（v1.0.1）**：属性名（`foreColor`、`backColor`、`isInclude` 等）与原版 TAT
  的互通性需在 M4 里程碑用原版 TAT 实测校验；如有差异只允许**扩展可选属性**，
  不允许改名既有属性（保持 §0.3 硬约束 6）。

### 7.3 `QSettings` 项

| Key | 类型 | 说明 |
| :--- | :--- | :--- |
| `geometry/main` | QByteArray | 主窗口几何 |
| `state/main` | QByteArray | Dock 布局状态 |
| `recentFiles` | QStringList | 最近 10 个文件 |
| `font/editor` | QFont | 编辑器字体 |
| `font/size` | int | 字号 |
| `tab/width` | int | Tab 宽度 |
| `filter/debounceMs` | int | 防抖窗口 |
| `filter/parallelThreads` | int | 匹配线程数（0 = 自动） |
| `language` | QString | 界面语言（`en_US` / `zh_CN`） |
| `session/restore` | bool | 启动时恢复上次会话 |

---

## 8. 并发模型

| 任务 | 执行线程 | 策略 | 取消 |
| :--- | :--- | :--- | :--- |
| 文件加载 + 行索引 | `QThreadPool` | `QtConcurrent::run`，进度回报 10% 一次 | 关闭文件时置 cancel flag |
| 过滤器匹配 | `QThreadPool` 分片 | `QtConcurrent::map`，双缓冲 | 规则变更时 cancel |
| 搜索 | `QThreadPool` | 单任务 | 用户按 Esc |
| UI 渲染 | GUI 主线程 | 仅绘制 FrontBuffer | N/A |
| 标记跳转 | GUI 主线程 | O(1) 查表 | N/A |
| 保存/加载 .tat | `QThreadPool` | v1.0.1 修订：与"MUST 主线程禁 I/O"一致，后台执行（< 1 MB 时耗时 < 1 ms，无感）；写前临时文件 + `rename` 原子替换 | 关闭对话框时取消 |

**MUST** GUI 主线程**绝对禁止**执行 I/O、正则、`mmap` 系统调用。所有耗时操作
通过 `QtConcurrent` 提交；进度通过 `QMutex`-protected 进度计数器 → `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 回主线程更新 UI。

---

## 9. 目录结构

```
textanalyst-qt/
├── CMakeLists.txt                  # 顶层 CMake
├── .clang-format                   # 格式化配置
├── .clang-tidy                     # 静态检查配置
├── .gitignore
├── build.sh                        # 一键构建脚本
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── core/                       # BLL + DAL，Qt-independent
│   │   ├── CMakeLists.txt
│   │   ├── buffer/
│   │   │   ├── MemoryMappedFile.h/cpp
│   │   │   ├── LineIndexer.h/cpp
│   │   │   └── TextBuffer.h/cpp
│   │   ├── engine/
│   │   │   ├── FilterEngine.h/cpp
│   │   │   ├── MarkerManager.h/cpp
│   │   │   └── Searcher.h/cpp
│   │   └── models/
│   │       ├── FilterRule.h
│   │       └── ResultState.h
│   ├── io/
│   │   ├── TatSerializer.h/cpp
│   │   └── SettingsManager.h/cpp
│   ├── controller/
│   │   ├── MainController.h/cpp
│   │   └── CommandLineParser.h/cpp
│   └── ui/
│       ├── CMakeLists.txt
│       ├── mainwindow/
│       │   ├── MainWindow.h/cpp
│       │   └── MainWindow.ui
│       ├── widgets/
│       │   ├── LogListView.h/cpp
│       │   ├── LogViewModel.h/cpp
│       │   ├── LogViewDelegate.h/cpp
│       │   ├── FilterListModel.h/cpp
│       │   └── FilterDockWidget.h/cpp
│       └── dialogs/
│           ├── PreferencesDialog.h/cpp
│           ├── FindDialog.h/cpp
│           └── AboutDialog.h/cpp
├── resources/
│   ├── icons/
│   │   ├── textanalyst-qt.svg
│   │   └── hicolor/                  # 16/24/32/48/64/128/256
│   ├── styles/
│   │   └── default.qss
│   └── translations/
│       ├── textanalyst-qt.ts
│       └── textanalyst-qt_zh_CN.ts
├── packaging/
│   ├── deb/
│   │   ├── control.template
│   │   ├── copyright
│   │   ├── changelog
│   │   ├── compat
│   │   ├── rules
│   │   ├── install/
│   │   │   ├── usr/share/applications/textanalyst-qt.desktop
│   │   │   ├── usr/share/mime/packages/textanalyst-qt.xml
│   │   │   └── usr/share/man/man1/textanalyst-qt.1
│   │   └── build-deb.sh
│   ├── appimage/
│   │   ├── AppDir/
│   │   └── build-appimage.sh
│   └── snap/
│       ├── snapcraft.yaml
│       └── snap/
├── tests/
│   ├── CMakeLists.txt
│   ├── unit/
│   │   ├── tst_buffer.cpp
│   │   ├── tst_filter.cpp
│   │   └── tst_marker.cpp
│   ├── integration/
│   │   └── tst_open_file.cpp
│   ├── performance/
│   │   └── bench_1gb.cpp
│   └── data/                        # 测试用大数据
│       ├── small.log
│       └── medium.log
└── docs/
    ├── architecture/
    │   ├── 01-blueprint.md            # 上游蓝皮书（原 ToolsFuncation.md）
    │   ├── 02-system-architecture.md  # 本文档（原 ARCHITECTURE.md）
    │   └── 03-detailed-design.md      # 详细设计（原 DISPLAYDESIGN.md）
    ├── BUILD.md
    └── PACKAGING.md
```

---

## 10. 构建系统（CMake）

### 10.1 顶层 `CMakeLists.txt` 关键片段

```cmake
cmake_minimum_required(VERSION 3.22)
project(textanalyst-qt VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Ubuntu 22.04+ 默认 Qt 6
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml Network)
find_package(Threads REQUIRED)

add_subdirectory(src)
add_subdirectory(tests)
install(TARGETS textanalyst-qt DESTINATION bin)
install(FILES packaging/deb/install/usr/share/applications/textanalyst-qt.desktop
        DESTINATION share/applications)
```

### 10.2 一键构建脚本 `build.sh`

```bash
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_TYPE="${1:-Release}"
BUILD_DIR="${ROOT}/build/${BUILD_TYPE}"

mkdir -p "${BUILD_DIR}"
cmake -S "${ROOT}" -B "${BUILD_DIR}" -G Ninja \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      -DCMAKE_PREFIX_PATH="/usr/lib/qt6" \
      -DCMAKE_INSTALL_PREFIX="/usr/local"
cmake --build "${BUILD_DIR}" -j "$(nproc)"
# cmake --install "${BUILD_DIR}"  # 需要 root
```

### 10.3 依赖安装（开发环境）

**Ubuntu 24.04**：

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  ninja-build \
  qt6-base-dev \
  qt6-base-dev-tools \
  qt6-tools-dev-tools \
  libqt6svg6-dev \
  libqt6opengl6-dev \
  libicu-dev \
  libpcre2-dev \
  pkg-config \
  clang-format \
  clang-tidy \
  fonts-noto-cjk
```

**Ubuntu 22.04**：同上，但 `qt6-base-dev` 版本为 6.4.2，可额外安装
`qt6-wayland-dev`（Wayland 平台插件开发）。

---

## 11. 打包与部署

### 11.1 `.deb`（首选，Ubuntu 原生）

- 使用 `dh_make` 初始化（见 `packaging/deb/`）。
- `debian/rules`：`override_dh_auto_install` 调用 `cmake --install`。
- `debian/control` 依赖（MUST 显式列出）：
  ```
  Depends: libc6 (>= 2.35), libqt6core6 (>= 6.4.2), libqt6gui6 (>= 6.4.2),
           libqt6widgets6 (>= 6.4.2), libqt6concurrent6, libqt6xml6,
           libpcre2-8-0 (>= 10.40)
  Suggests: fonts-noto-cjk
  ```
- **MUST** `multiarch` 包名（`libqt6core6` 而非 `qt6-base`）；`shlibdeps`
  自动生成。
- 上传到私有 apt 仓库或 GitHub Releases 的 `.deb` 附件。

### 11.2 AppImage（便携，可选）

- 使用 `linuxdeployqt`（Ubuntu apt 包 `linuxdeployqt-qt6`）。
- 打包后单文件 `TextAnalyst-Qt-x.y.z-x86_64.AppImage`。
- **MUST** 嵌入 `desktop-file` 与图标，确保在 nautilus 中双击可直接打开 `.log`。
- 适用于"不想装 .deb 但想跑程序"的场景。

### 11.3 Snap（可选）

- `snapcraft.yaml`：`base: core22`，`confinement: strict`。
- 优势：自动更新；劣势：包体积大、启动慢（snapd 解包）。
- **SHOULD** 仅在有自动更新需求时启用。

### 11.4 不提供的打包形式

- **N/A**：Windows `.msi` / `.exe`、macOS `.dmg` / `.app`、通用 zip 发布。
- **N/A**：RPM、Flatpak（如需可通过社区 PR，不进主干）。

---

## 12. 系统集成（Ubuntu）

### 12.1 `.desktop` 文件

```
[Desktop Entry]
Type=Application
Name=TextAnalyst-Qt
GenericName=Text Log Analyzer
Comment=High-performance text filtering & marking tool
Exec=textanalyst-qt %F
Terminal=false
Categories=TextEditor;Development;Utility;
MimeType=text/plain;text/x-log;
Icon=textanalyst-qt
StartupWMClass=textanalyst-qt
Keywords=log;filter;mark;regex;
```

- `MimeType` 触发 Ubuntu 文件管理器右键"用 TextAnalyst-Qt 打开"。
- `StartupWMClass` 确保 Activities 视图正确分组。

### 12.2 MIME 注册

`packaging/deb/install/usr/share/mime/packages/textanalyst-qt.xml`：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="text/x-log">
    <comment>Text log file</comment>
    <glob pattern="*.log"/>
    <glob pattern="*.txt"/>
  </mime-type>
</mime-info>
```

安装后运行 `update-mime-database /usr/share/mime` 与
`update-desktop-database /usr/share/applications`。

### 12.3 Man 页面

`usr/share/man/man1/textanalyst-qt.1`（groff 格式），覆盖所有命令行选项与
环境变量（`QT_SCALE_FACTOR`、`QT_QPA_PLATFORM`、`LANG`）。

### 12.4 自动启动（可选）

提供 `textanalyst-qt.desktop` 到 `~/.config/autostart/`，但默认**不启用**
（日志分析器不是常驻程序）。

---

## 13. 性能预算（Ubuntu 硬件基线）

测试硬件：Ubuntu 24.04，x86_64，8 vCPU，16 GB RAM，NVMe SSD。

| 指标 | 目标 | 测量方法 |
| :--- | :--- | :--- |
| 冷启动（首屏绘制） | < 300 ms | `time` + `perf trace` |
| 打开 1 GB 文件 | < 500 ms（含索引构建） | `tst_open_file.cpp` |
| 索引 1 亿行 | < 2 s | `bench_1gb.cpp` |
| 单条正则全量匹配 1 亿行 | < 3 s（可取消） | `bench_1gb.cpp --regex` |
| 滚动帧率（100 万行文件的实时滚动） | ≥ 60 fps | `paint()` 内 perf 采样 |
| 标记跳转（1 亿行） | < 1 ms | `tst_marker.cpp` |
| 内存占用（1 GiB 文件，全文件访问后） | ≤ 1.4 GiB（v1.0.1 复核：原 1.1 GiB 未计入 mmap 页 + LineMeta 索引结构，修订见 DISPLAYDESIGN §9.2；典型视口场景 Rss < 350 MB） | `/proc/self/smaps_rollup` |
| 磁盘占用（.deb） | < 2 MB | `du -b` |
| AppImage 大小 | < 60 MB | `du -b` |

**MUST**：性能回归在 CI 中跑 `tst_open_file.cpp` 与 `bench_1gb.cpp`，超过阈值
2× 即阻断合并。

---

## 14. 安全与稳定性

| 风险 | 对策 |
| :--- | :--- |
| mmap 文件被外部修改 | `fcntl` 读锁 + `stat()` mtime 采样 + inotify 提示；不自动重载 |
| 正则回溯 DoS | 3 秒超时 + 用户可停止 + 空模式拒绝（v1.0.1 修订：`DontMatchEmptyString` 枚举在 Qt 6.4 不存在） |
| 路径穿越 | 打开文件前 `QFileInfo::canonicalFilePath()` 规范化 |
| ANSI/GBK 误判 | 严格按 3.3 优先级；BOM 优先 |
| OOM | 检测 `/proc/meminfo` `MemAvailable` < 15% 时 `madvise(MADV_DONTNEED)` |
| ASan/UBSan 泄漏 | Debug 构建 `CMAKE_CXX_FLAGS="-fsanitize=address,undefined"`；CI 跑 Debug 版本 |
| 崩溃日志 | 安装 `QCoreApplication::installMessageHandler` 写入 `~/.cache/textanalyst-qt/crash.log`；可选上报（默认关闭） |
| Wayland 剪贴板空指针 | 检查 `QClipboard::supportsSelection()` 后再操作 |

---

## 15. 测试策略

| 层级 | 工具 | 目标 | CI 阻断 |
| :--- | :--- | :--- | :--- |
| 单元 | `QtTest` | 100% 行覆盖 BLL/DAL | 是 |
| 集成 | `QtTest` + 真实 mmap | 打开 1 MB / 100 MB / 1 GB 三档 | 是 |
| 性能 | 自定义 benchmark | 性能预算全部达标 | 是（阈值 2×） |
| 打包 | `lintian` + `dpkg --lintian` | 无 error/warning | 是 |
| 包冒烟 | `sudo dpkg -i` + `textanalyst-qt --version` | 安装后命令可用 | 是 |
| 静态 | `clang-tidy` / `clang-format --dry-run` | 0 warning | 否（仅警告） |
| 沙箱 | `qemu-user`（可选 arm64） | 跨架构编译通过 | 否 |

### 15.1 CI（GitHub Actions 示意）

```yaml
on: [push, pull_request]
jobs:
  test-ubuntu:
    strategy:
      matrix:
        ubuntu: [22.04, 24.04]
    runs-on: ubuntu-${{ matrix.ubuntu }}
    steps:
      - uses: actions/checkout@v4
      - name: Install deps
        run: |
          sudo apt update
          sudo apt install -y build-essential cmake ninja-build \
            qt6-base-dev qt6-tools-dev-tools libicu-dev libpcre2-dev
      - name: Build
        run: cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
      - name: Unit tests
        run: ctest --test-dir build --output-on-failure
      - name: Performance gate
        run: ./build/Release/tests/performance/bench_1gb --threshold 2.0
      - name: Build .deb
        run: bash packaging/deb/build-deb.sh
      - uses: actions/upload-artifact@v4
        with:
          name: textanalyst-qt_${{ matrix.ubuntu }}_deb
          path: packaging/deb/*.deb
```

---

## 16. 里程碑与路线图

| 里程碑 | 内容 | 验收标准 |
| :--- | :--- | :--- |
| **M1**（2 周） | DAL：mmap + LineIndexer + 编码检测 | 1 GB 文件 500 ms 打开；单测覆盖 90% |
| **M2**（3 周） | BLL：FilterEngine + MarkerManager + Searcher | 1 亿行匹配 < 3 s；取消协议可用 |
| **M3**（3 周） | UI：LogListView + Delegate + Dock + 偏好 | 100 万行滚动 60 fps |
| **M4**（2 周） | 持久化 + 命令行 + 系统集成 | .tat 与原版 TAT 互通；`.deb` 安装可用 |
| **M5**（2 周） | 打包 + 性能门槛 + i18n | CI 全绿；性能预算达标；中英文双语 |
| **M6**（持续） | 文档 + 插件接口 + 性能优化 | 发布 v1.0 |

---

## 17. 关键决策记录（ADR 摘要）

| ID | 决策 | 理由 |
| :--- | :--- | :--- |
| ADR-001 | 仅支持 Ubuntu，不支持 Windows/macOS | 降低测试矩阵，避免跨平台抽象层 |
| ADR-002 | Qt 6.4+ 为主，Qt 5 仅 `qt5-compat` 分支 | Qt 6 Wayland 一等公民；Qt 5 维护到 2027 |
| ADR-003 | 原生 POSIX `mmap` 而非 `QFile::map` | 规避 QFile 生命周期耦合；更直接的 madvise 控制 |
| ADR-004 | 索引层用字节偏移 + 渲染时转码 | 保证 ANSI/UTF-8 跨编码索引一致 |
| ADR-005 | 双缓冲用 `std::atomic<T*>` 原子交换 | 无锁；渲染线程零等待 |
| ADR-006 | 匹配线程 `nice(19)` | 不抢占 UI 主线程；用户体感优先 |
| ADR-007 | `.deb` 为首选，AppImage 次选 | Ubuntu 原生；apt 自动依赖解析 |
| ADR-008 | XDG Base Directory 严格遵循 | 与 GNOME/Ubuntu 文件管理器集成 |
| ADR-009 | 正则 3 s 超时 + 用户可取消 | 防 DoS；保留用户控制权 |
| ADR-010 | GUI 主线程绝不 I/O/正则/mmap | UI 丝滑的硬约束 |

---

## 18. 风险登记册

| 编号 | 风险 | 概率 | 影响 | 缓解 |
| :--- | :--- | :--- | :--- | :--- |
| R1 | Ubuntu 24.04 之后 Qt 6.4 被升级到 6.6+ 引入 ABI 变化 | 中 | 高 | 锁 `find_package(Qt6 6.4)`；发布时同时打 `qt6.4` 与 `qt6.6` 两包 |
| R2 | Wayland 下系统剪贴板 API 行为不一致 | 中 | 中 | 单元测覆盖 `supportsSelection()`；提供 X11 回退 |
| R3 | 1 GB 文件索引内存超预算 | 低 | 中 | `madvise(MADV_DONTNEED)` + 分段索引 |
| R4 | GB18030 大文件编码检测误判 | 中 | 低 | BOM 优先 + iconv 严格校验 + ICU 兜底 |
| R5 | 正则回溯导致 CPU 100% | 高 | 中 | 3 s 超时 + 空模式拒绝 + `nice(19)` |
| R6 | `.deb` 依赖 libpcre2 版本漂移 | 低 | 中 | 显式 `Depends: libpcre2-8-0 (>= 10.40)` |
| R7 | 输入法（fcitx5）在 Wayland 下偶发不工作 | 中 | 低 | 上游问题，文档说明 `QT_IM_MODULE=fcitx` |
| R8 | CI runner 升级到 Ubuntu 24.04 后 Qt 包变化 | 高 | 中 | 双矩阵（22.04 + 24.04）锁版本 |

---

## 19. 附录

### 19.1 完整 apt 安装命令（Ubuntu 24.04）

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-base-dev-tools qt6-tools-dev-tools \
  libqt6svg6-dev libqt6opengl6-dev libqt6wayland-dev \
  libicu-dev libpcre2-dev \
  clang-format clang-tidy \
  fonts-noto-cjk \
  linuxdeployqt-qt6 \
  lintian
```

### 19.2 常用运行环境变量

```bash
export QT_QPA_PLATFORM=xcb           # 强制 X11（Wayland 调试时）
export QT_SCALE_FACTOR=1             # HiDPI 调试
export QT_LOGGING_RULES="*.*=true"   # 全量日志
export LANG=zh_CN.UTF-8              # 中文界面
export QT_IM_MODULE=fcitx            # fcitx 输入法
```

### 19.3 故障排查命令

```bash
# 检查 Qt 库
ldd $(which textanalyst-qt) | grep -i qt
# 查看内存占用
watch -n1 "grep -E 'Rss|Anon|Mapped' /proc/\$(pidof textanalyst-qt)/smaps_rollup"
# 查看 mmap 占用
grep VmPTE /proc/$(pidof textanalyst-qt)/status
# 查看 inotify watch 数量
cat /proc/sys/fs/inotify/max_user_watches
# 查看 CPU 亲和性
taskset -p $(pidof textanalyst-qt)
# 生成 crash log
QT_FATAL_WARNINGS=1 textanalyst-qt --verbose
```

### 19.4 术语表

| 术语 | 含义 |
| :--- | :--- |
| DAL / BLL / UI / Controller | 数据访问层 / 业务逻辑层 / 表现层 / 控制层 |
| mmap | POSIX 内存映射，把文件直接映射到进程虚拟地址空间 |
| madvise | POSIX 提示系统如何管理已映射内存 |
| inotify | Linux 文件系统变更通知 |
| FNV-1a | 快速非加密哈希，用于行去重 |
| PCRE2 | Perl-Compatible Regular Expressions v2，Qt 6 默认正则后端 |
| XDG | X Desktop Group 规范，定义用户配置/数据路径 |
| Adwaita / Fusion | GNOME / Qt 内置主题 |
| Wayland / X11 | Linux 显示协议 |
| HiDPI | 高像素密度显示 |

### 19.5 与蓝皮书的差异对照

| 蓝皮书章节 | 本文档对应 | 主要变更 |
| :--- | :--- | :--- |
| 1 分层 | 2 | 加入依赖倒置表与 Ubuntu 通信机制 |
| 2 DAL | 3 | 原生 POSIX mmap、fcntl 锁、inotify、madvise |
| 3 BLL | 4 | nice()、CPU 亲和、双缓冲原子交换 |
| 4 UI | 5 | Wayland/X11 适配、HiDPI、Noto CJK、Qt6 风格 |
| 5 Controller | 6 | QCommandLineParser、信号处理、优雅退出 |
| 6 持久化 | 7 | XDG 路径、原子写入、.tat 兼容 |
| 7 并发 | 8 | nice + 进度回报机制 |
| 8 类图 | — | 由代码注释与头文件维护，不入文档 |
| 9 风险 | 14/18 | 拆分为"安全措施"与"风险登记册" |
| 10 目录 | 9 | 加入 resources / packaging / tests / docs |
| — | 10/11/12 | **新增**：构建系统、打包部署、系统集成 |
| — | 13/15 | **新增**：性能预算、测试策略 |
| — | 16/17/19 | **新增**：路线图、ADR、附录 |

---

## 20. 修订历史

| 版本 | 日期 | 变更 | 作者 |
| :--- | :--- | :--- | :--- |
| v1.0-ubuntu | 初版 | 基于蓝皮书 v1.0 收敛到 Ubuntu 单一平台 | — |
| v1.0.1 | 2026-08-29 | 审核修订：①`std::span`→指针+计数（C++17）；②`EncodingDetector` 归入 DAL；③BLL 经 `ThreadPool` 抽象接入并发；④`MemoryMappedFile` 接口补全（MmapAdvise 未定义）；⑤双缓冲改 `SharedSnapshot`（防 use-after-free）；⑥`MarkerManager` 改有序 vector；⑦信号处理改自管道模式（async-signal-safe）；⑧并发模型表消除".tat 主线程 I/O"自相矛盾；⑨内存预算复核为 ≤1.4 GiB；⑩.7.2 加原版 TAT 互通性验证注记 | 评审修订 |

> 下一步建议：按 M1 开始 DAL 实现，同步创建 `CMakeLists.txt`、`build.sh`、
> `tests/unit/tst_buffer.cpp`，并在 CI 中固化 Ubuntu 22.04/24.04 双矩阵。
