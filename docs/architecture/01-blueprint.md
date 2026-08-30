# TextAnalyst-Qt 软件架构设计蓝皮书 v1.0

> **文档定位说明（2026-08-29 增补，正文未改动）**
> 本文档是概念蓝皮书，记录设计意图。它属于"上游参考"，与约束级文档
> `docs/architecture/02-system-architecture.md`（v1.0.1-ubuntu）及接口级文档
`docs/architecture/03-detailed-design.md`（v1.1）冲突之处，
> **以这两份文档为准**（完整裁决表见 DISPLAYDESIGN §0.4）。典型裁决：
> - `QFile::map()` → 原生 POSIX `mmap()`（ARCH-UB §3.1）
> - `MarkerManager` 用 `unordered_set` → 有序 `std::array<std::vector<int>,8>`（DISPLAYDESIGN §3.4）
> - 命令行 `/Line:100`、`/clipboard` → `QCommandLineParser` 长选项（ARCH-UB §6.2）
> - `LogListView` 继承 `QAbstractItemView` → 继承 `QListView`（DISPLAYDESIGN §7.4）
> - 标记快捷键数字键 `3` → `Ctrl+1..8` 切换 / `Alt+1..8` 跳转（ARCH-UB §4.2）

## 1. 总体架构分层 (Layered Architecture)

为了逻辑清晰，我们将系统划分为 4 层，依赖关系自上而下：

- **UI 表现层 (Presentation Layer)**：基于 Qt 5.15/6.x 的 `QMainWindow` 主窗口，含核心编辑器、停靠面板、状态栏。**绝不**包含业务逻辑。
- **控制层/视图模型 (Controller/ViewModel)**：负责接收用户操作（点击、快捷键、拖拽），调度后台任务，连接 UI 与数据。采用 **中介者模式** 解耦各面板通信。
- **核心业务逻辑层 (Business Logic Layer)**：**这是系统的灵魂**。包含过滤引擎、标记管理、正则匹配器。该层**完全独立于 Qt UI**，仅依赖 STL 和标准 C++17。
- **数据访问层 (Data Access Layer)**：负责文件内存映射（`mmap`）、行索引构建、编码识别（UTF-8/16/ANSI）。

---

## 2. 数据访问层 (DAL) —— 极速加载的基石

**设计目标**：1GB 文件在 500ms 内打开完毕，内存占用不超过文件物理大小 + 10%。

### 2.1 核心类：`MemoryMappedFile`
- **原理**：使用 POSIX 标准的 `mmap`（Qt 封装为 `QFile::map()`），将硬盘文件直接映射到进程的虚拟地址空间。不进行 `fread` 拷贝。
- **职责**：返回文件起始指针 `const char* rawData` 和总大小 `size_t fileSize`。

### 2.2 核心类：`LineIndexer`
**关键设计**：原版 TAT 将所有行转为 `String` 对象存入 `List`，内存膨胀严重。我们采用 **“延迟解析 + 偏移量索引”** 策略。

- **数据结构**：
  ```cpp
  struct LineMeta {
      uint32_t offset;    // 该行在 mmap 内存中的起始字节偏移量
      uint32_t length;    // 该行的字节长度 (不含换行符)
      uint32_t hash;      // 用于快速去重或缓存标识 (可选)
  };
  class TextBuffer {
      std::vector<LineMeta> lines;        // 所有行的元数据，预分配容量
      const char* basePtr;                // mmap 基址
      QAtomicInt dirtyFlag;               // 标记是否被修改
  };
  ```
- **索引构建流程**：在后台线程扫描 `mmap` 内存，识别 `\n` 或 `\r\n`，将偏移量和长度填入 `vector`。因为 `vector` 内存连续，对 CPU 缓存极其友好。

### 2.3 编码处理
- 读取文件头部 BOM 或使用 `QStringConverter` 探测编码。若为 ANSI，使用 `QString::fromLocal8Bit` 仅**在渲染时转换**，索引层仍存储原始 UTF-8/字节偏移，确保跨平台一致性。

---

## 3. 核心业务逻辑层 (BLL) —— 过滤与标记引擎

**设计理念**：**数据驱动，无状态函数**。该层不持有 UI 指针，只操作 `TextBuffer` 和规则集。

### 3.1 类：`FilterEngine`
- **规则存储**：
  ```cpp
  enum class FilterAction { Include, Exclude };
  enum class MatchMode { Substring, Regex, Marker };
  
  struct FilterRule {
      int id;
      FilterAction action;
      MatchMode mode;
      QString pattern;                    // 原始输入
      QRegularExpression compiledRegex;   // 预编译正则 (仅当 mode==Regex)
      QColor foreground;
      QColor background;
      bool isEnabled;
      int matchCount;                     // 命中行数统计
  };
  ```
- **匹配执行策略 (多线程分治)**：
  当用户点击“应用过滤器”时，不阻塞 UI。启动 `QThreadPool` 后台任务：
  - 将总行数按 CPU 核心数分片（Chunk）。
  - 每个线程处理一片，运行 `QRegularExpression::match()`。
  - 结果写入 `std::vector<ResultState>` 数组（状态枚举：`Hidden`, `Dimmed`, `Highlighted`），每个元素仅占 1 字节。
  - **双缓冲交换 (Double Buffering)**：后台计算填充 `BackBuffer`，完成后原子交换指针给 `FrontBuffer`，UI 渲染线程瞬间读到最新结果，无锁无等待。

### 3.2 类：`MarkerManager`
- **存储**：`std::array<std::unordered_set<int>, 8>` 存储 8 种标记对应的行号索引。
- **操作复杂度**：添加/删除标记为 O(1) 平均复杂度。按下数字键 `3` 时，查找当前行号在 `unordered_set` 中的下一项，实现循环跳转。

### 3.3 类：`Searcher`
- **增量搜索**：独立于过滤器。使用 Boyer-Moore-Horspool 算法（Qt 的 `QString::indexOf` 已优化）或正则。
- **高亮叠加**：搜索结果高亮独立于过滤器高亮，两者通过位掩码叠加（Filter 颜色 + Search 下划线）。

---

## 4. UI 表现层 (View) —— 百万行滚动的秘密

**核心难点**：`QListWidget` 或 `QTextEdit` 在加载 100 万行时会卡死。必须使用 **虚拟化 (Virtualization)**。

### 4.1 自定义控件：`LogListView` (继承 `QAbstractItemView`)
- **模型 (Model)**：继承 `QAbstractListModel`，重写 `rowCount()` 返回总行数，`data()` 返回 `Qt::DisplayRole`。
- **最关键——渲染委托 (Delegate)**：
  - 继承 `QStyledItemDelegate`，重写 `paint()` 方法。
  - **按需截取**：根据传入的 `QModelIndex`，计算对应行号。通过行号索引 `LineMeta`，直接从 `mmap` 内存中截取 `const char*` 段，使用 `QString::fromUtf8` 转换为显示字符。
  - **颜色渲染**：检查 `FrontBuffer` 状态数组：
    - 若状态为 `Hidden`：`painter->setOpacity(0)` 或直接 `continue`（不绘制）。
    - 若为 `Dimmed`：设置灰色画笔。
    - 若为 `Highlighted`：填充 `FilterRule` 对应的 `QBrush` 背景。
  - **性能优化**：`paint()` 内部绝不进行正则匹配或复杂计算，只做**查表 + 内存拷贝**。

### 4.2 主窗口布局 (Fully Docking)
- **中央区域**：`LogListView` 占满。
- **右侧停靠窗 (QDockWidget)**：过滤器列表 `QListView`（自定义委托显示颜色方块 + 命中计数 `[x/y]`）。
- **底部/顶部**：添加过滤器的输入栏，包含 `QLineEdit`、`QCheckBox`（Include/Exclude/Regex/Case）、`QPushButton`（颜色选择）。

### 4.3 信号槽防抖 (Debounce)
- 用户在 `QLineEdit` 输入时，`textChanged` 信号触发一个 **200ms 的单次定时器 (`QTimer::singleShot`)**。定时器超时后再提交后台过滤任务。**严禁**在输入过程中频繁启动匹配线程。

---

## 5. 控制层 (Controller) —— 路由与状态管理

- **类：`MainController`** (单例或由主窗口构造)
  - 职责：持有 `TextBuffer`、`FilterEngine`、`MarkerManager` 实例。
  - 协调逻辑：接收 `LogListView` 的“双击行”事件 → 调用 `FilterEngine::addRuleFromLine(lineContent)` → 刷新视图。
  - 命令行解析：支持 `/Line:100` 跳转，`/clipboard` 加载剪贴板。

---

## 6. 持久化与配置

### 6.1 过滤器存储 (.tat 文件)
- 使用 **Qt XML 模块 (`QXmlStreamWriter`)** 生成格式化的 XML，与原版 TAT 标签属性保持一致（`foreColor`, `backColor`, `pattern`, `isInclude` 等），确保与原有生态互通。
- 加载时使用 `QXmlStreamReader` 流式解析，不一次性加载 DOM 树以节省内存。

### 6.2 用户偏好 (QSettings)
- 存储在 `~/.config/MyTextAnalyst.conf` 或注册表（跨平台抽象）。
- 存储项：窗口几何、最近打开文件列表、缩放级别、字体名称、Tab 宽度。

---

## 7. 并发模型 (Concurrency) —— 保持 UI 丝滑

| 任务类型 | 执行线程 | 策略 |
| :--- | :--- | :--- |
| **文件加载/行索引** | 全局后台线程 (`QtConcurrent::run`) | 加载时主窗口显示“Loading...”动画，加载完成信号触发 `resetModel`。 |
| **过滤器应用** | `QThreadPool` 分片并发 | 任务可取消（当用户修改规则时，取消旧任务，提交新任务）。 |
| **UI 渲染/滚动** | GUI 主线程 | **绝对禁止**在此线程执行 I/O 或正则运算。只负责绘制 `FrontBuffer` 状态。 |
| **标记跳转** | GUI 主线程 | O(1) 查表，瞬时完成。 |

---

## 8. 类图概览 (简化 UML)

```text
[MainWindow]  -->  [MainController]
      |                    |
      |                   \/
      |           [TextBuffer] (mmap + LineMeta)
      |                   |
      |                   \/
      |           [FilterEngine] (持有 FilterRule 列表)
      |                   |
      |                   \/
      |           [MatchWorker] (后台分片执行)
      |                   |
      |                   \/
      +--> [LogListView] --+--> [LogViewDelegate] (读取 FrontBuffer 渲染)
      |
      +--> [FilterDockWidget] (管理规则 UI)
```

---

## 9. 关键技术风险与对策

- **风险1：mmap 文件被外部修改**。
  - 对策：使用 `fcntl` 加读锁，或在加载时计算文件 CRC 做校验。TAT 原版做法是“不管外部，只显示加载时的快照”，我们沿用这一逻辑以保持简单。
- **风险2：超大正则匹配导致 CPU 100%**。
  - 对策：后台匹配线程设为 `LowPriority`；若匹配超过 3 秒，自动弹窗提示用户简化正则，并提供“停止匹配”按钮。
- **风险3：Qt 5.15 与 Ubuntu 20.04/22.04 的 ABI 兼容性**。
  - 对策：建议使用 Qt 6.x 并静态链接关键库，或提供动态编译版本（依赖系统 `libqt5core.so`，符合你之前“轻依赖”的要求）。

---

## 10. 文件目录结构建议

```text
src/
├── main.cpp
├── core/
│   ├── buffer/ (MMapFile, LineIndexer)
│   ├── engine/ (FilterEngine, MarkerManager, Searcher)
│   └── models/ (FilterRule, ResultState)
├── ui/
│   ├── mainwindow/
│   ├── widgets/ (LogListView, LogViewDelegate)
│   └── dialogs/ (PreferencesDialog, FindDialog)
├── controller/ (MainController, CommandLineParser)
├── io/ (TatSerializer, SettingsManager)
└── utils/ (DebounceTimer, ThreadPoolWrapper)
```

---

