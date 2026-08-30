# 构建与测试说明（BUILD）

## 1. 依赖（Ubuntu 22.04 / 24.04）

完整构建（Qt GUI 应用 + 全部测试）：

```bash
sudo apt install -y build-essential cmake \
  qt6-base-dev qt6-base-dev-tools \
  libicu-dev libpcre2-dev fonts-noto-cjk
```

限 core 层（无 Qt 环境，如 CI 沙箱/容器）：**仅需 g++**（`tools/check-core.sh`）。

## 2. 构建

```bash
./build.sh Release          # 生成器自动选择 Ninja 或 Unix Makefiles
./build.sh Debug            # Debug（textanalyst-qt 自动附加 ASan/UBSan）
```

产物：`build/<type>/src/textanalyst-qt`；测试：`build/<type>/tests/*`。

## 3. 测试

```bash
ctest --test-dir build/Release --output-on-failure
```

| 测试 | 层次 | 内容 |
| :--- | :--- | :--- |
| tst_buffer / tst_encoding | DAL | mmap、BOM/UTF-8/GBK、CRLF/CR、取消、空文件语义 |
| tst_marker / tst_filter / tst_search | BLL | 状态决策表 8 场景、词边界、并发一致性、字节偏移 |
| tst_io | IO | .tat 往返/备份/坏文件、QSettings 会话、inotify 事件 |
| tst_open_file | 集成 | 1MB/50MB/GBK 真实文件 + Controller 异步流 |
| bench_1gb | 性能 | 打开/过滤速率与内存（`bench_1gb --size 1024`） |

无 Qt 环境下的 core 层验证：

```bash
tools/check-core.sh            # 全部 core 单测
tools/check-core.sh --asan     # ASan/UBSan 版本
tools/check-core.sh --bench 256
```

## 4. GUI 冒烟（无显示环境）

```bash
QT_QPA_PLATFORM=offscreen ./build/Release/src/textanalyst-qt \
  --file sample.log --grep ERROR --export out.txt
# 之后向进程发 SIGTERM 验证优雅退出（自管道 → quit → 保存会话）
```

## 5. 目录结构（分层依赖：ui → controller → io → core）

```
src/core/models   基础类型（common/Error/Task/LineMeta/RowState/FilterRule/...）
src/core/buffer   DAL：MemoryMappedFile/EncodingDetector/LineIndexer/TextBuffer
src/core/engine   BLL：ThreadPool/FilterEngine/MarkerManager/Searcher/ResultStore
src/io            TatSerializer/SettingsManager/FileWatcher
src/controller    MainController/CommandLineParser
src/ui            widgets（视图与委托）/ mainwindow / dialogs
src/main.cpp      装配入口（命令行、信号处理、会话）
tests/unit        纯 core 单测（minitest，无 Qt）
tests/integration Qt 集成测试（QtTest）
tests/performance bench_1gb
tools/check-core.sh  无 Qt 环境验证脚本
```

约束（违反即评审退回，DISPLAYDESIGN §0.3）：

- `src/core/` 零 Qt 依赖（BLL 纯 STL+C++17；DAL 仅 POSIX）
- GUI 主线程不做 I/O/正则/mmap 系统调用
- 索引层一律 UTF-8 字节偏移；路径一律 `QStandardPaths`
- `.tat` 格式与原版 TAT 互通（仅允许扩展可选属性）