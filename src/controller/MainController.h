// =============================================================================
// MainController.h: 控制层核心（中介者：UI ↔ BLL/DAL）
// 文档：DISPLAYDESIGN.md §6（命令路由 §6.3 / 会话 §6.4 / 退出流程 §6.6）
// 线程安全：
//   命令方法 [M] 主线程；快照方法 [A] 任意线程（SharedSnapshot 无锁）
//   BLL 任务全部经 QThreadPool 后台执行，结果 QueuedConnection 回主线程
// =============================================================================
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>
#include <vector>

#include "core/buffer/TextBuffer.h"
#include "core/engine/FilterEngine.h"
#include "core/engine/MarkerManager.h"
#include "core/engine/ResultStore.h"
#include "core/engine/Searcher.h"
#include "core/models/Error.h"
#include "core/models/Task.h"
#include "core/models/common.h"

namespace tat {

class SettingsManager;
class FileWatcher;

class MainController : public QObject {
    Q_OBJECT
public:
    explicit MainController(QObject* parent = nullptr);
    ~MainController() override;

    // ===== 命令（[M] 主线程，立即返回；耗时操作异步，结果经信号回传）=====

    void openFile(const QString& path);
    void applyFilter(const std::vector<FilterRule>& rules);
    bool toggleMarker(int line, int markerId);           // 同步 O(log n)
    bool jumpToLine(int lineNo);
    bool jumpToNextMarker(int lineNo, int markerId);
    void search(const SearchOptions& opt);
    // skipDimmed：跳过 Dimmed 行（R-19 后引擎不再产生，保留参数兼容）
    Error exportLines(int startLine, int endLine, const QString& path,
                      bool skipDimmed = false);
    // 导出当前【匹配行】（Highlighted）——--grep + --export 组合的 grep 直觉语义
    Error exportMatching(const QString& path);
    Error loadFilters(const QString& path, std::vector<FilterRule>* out);
    Error saveFilters(const QString& path, const std::vector<FilterRule>& rules);

    void cancelCurrentTask();

    // ===== 快照（渲染路径无锁读，§5.4/§5.5）=====

    std::shared_ptr<const TextBuffer>   bufferSnapshot() const noexcept;
    std::shared_ptr<const FilterResult> resultSnapshot() const noexcept;
    std::shared_ptr<const RuleSet>      rulesSnapshot() const noexcept;

    // ===== 状态查询（[M]）=====

    int  rowCount() const noexcept;
    int  currentLine() const noexcept { return m_currentLine; }
    TextBuffer::MemoryFootprint memoryFootprint() const;
    MarkerManager& markers() { return m_markers; }
    std::vector<FilterRule> currentRules() const { return m_currentRules; }

    // ===== 会话（[M]）=====

    void saveSession() const;
    void restoreSession();

    // ===== 依赖注入（构造时默认 Qt 正则实现，§4.1）=====

    void setCompileFn(CompileFn fn) { m_compile = std::move(fn); }
    void setMatchFn(MatchFn fn) { m_match = std::move(fn); }
    void setSettingsManager(SettingsManager* s) { m_settings = s; }
    void setFileWatcher(FileWatcher* w) { m_watcher = w; }

signals:
    void fileOpened(const QString& path, int rowCount);
    void fileOpenFailed(const QString& path, const QString& op,
                        const QString& message);
    void filterApplied(bool ok, const QString& message);
    void filterProgress(double percent);
    void searchCompleted(const SearchResult& result);
    void markerToggled(int line, int markerId, bool added);
    void bufferChanged();               // TextBuffer 已替换，UI 需 resetModel
    void currentLineChanged(int lineNo);
    void statusMessage(const QString& msg);
    void regexTimeout(int ruleId, const QString& pattern);

private:
    // 后台任务完成回调（invokeMethod QueuedConnection 回主线程）
    void onOpenFinished(std::shared_ptr<TextBuffer> tb, Error err,
                        const QString& path);
    void onFilterFinished(std::shared_ptr<FilterResult> result, Error err,
                          int generation);

    // ===== 核心成员 =====

    SharedSnapshot<const TextBuffer>           m_buffer;
    ResultStore                                m_resultStore;
    SharedSnapshot<const RuleSet>              m_rules;
    MarkerManager                              m_markers;

    std::vector<FilterRule> m_currentRules;   // [M] 当前规则列表（供导出复用）
    Token                   m_token;
    int                     m_currentLine = 1;
    QString                 m_currentPath;

    CompileFn         m_compile;             // 构造时默认 Qt 正则（§4.1）
    MatchFn           m_match;
    SettingsManager*  m_settings = nullptr;
    FileWatcher*      m_watcher  = nullptr;
};

}  // namespace tat