// =============================================================================
// MainWindow.h: 主窗口（Fusion 主题 / Docking / 状态栏 / 拖放）
// 文档：DISPLAYDESIGN.md §7.6；ARCHITECTURE.md §5.2/§5.3
// =============================================================================
#pragma once

#include <QMainWindow>

#include "core/models/FilterRule.h"
#include "controller/MainController.h"

class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QTimer;
class QAction;
class QMenu;

namespace tat {

class FilterDockWidget;
class FilterListModel;
class FilterRuleDialog;
class FindDialog;
class LogViewDelegate;
class LogViewModel;
class LogListView;
class SettingsManager;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(MainController* controller,
                        SettingsManager* settings, QWidget* parent = nullptr);

    void openFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onFileOpened(const QString& path, int rowCount);
    void onFileOpenFailed(const QString& path, const QString& op,
                          const QString& message);
    void onFilterApplied(bool ok, const QString& message);
    void onOpenTriggered();
    void onSaveFiltersTriggered();
    void onLoadFiltersTriggered();
    void onExportTriggered();
    void onFindTriggered();
    // 双击日志行 → 弹出"添加过滤规则"弹窗（预填行文本，R-22）
    void onLineDoubleClicked(int lineNo);
    void onAddRuleRequested();        // Filters 菜单 / 双击日志行
    void onEditRuleRequested(int row);    // 双击规则行 / 菜单"修改选中"
    void onRemoveRuleRequested(int row);
    void onSearchRequested(const SearchOptions& opt);
    void onSearchCompleted(const SearchResult& result);
    void onIncrementalSearch(const SearchOptions& opt);  // 视口 ±500 行即时计数
    void onRulesListChanged();
    void status(const QString& message, int timeoutMs = 4000);  // 页脚右侧反馈

private:
    void setupActions();
    void setupStatusBar();
    void buildMenus();   // R-23/R-24：视图/Filters 菜单（按钮挂工具条单行）
    void applyCurrentRules();
    // R-22：规则编辑弹窗（添加/修改共用）；editId>=0 为修改模式
    void openRuleDialog(const FilterRule& init, int editId);
    void jumpToSearchHit(int direction);   // F3/Shift+F3 搜索循环
    // R-23：Filters 菜单（并入 buildMenus）
    void jumpToMatch(int direction);       // F8/Shift+F8 匹配行循环跳转
    void onEnableAllFilters(bool enabled); // 全部启用/禁用
    void onRemoveAllFilters();             // 清除全部规则
    int  selectedRuleRow() const;          // 面板当前选中规则行（-1 无）
    void collectRules(std::vector<FilterRule>* out) const;
    void updateStatusBar();

    MainController*  m_controller;
    SettingsManager* m_settings;

    LogListView*     m_view = nullptr;
    LogViewModel*    m_model = nullptr;
    LogViewDelegate* m_delegate = nullptr;
    FilterDockWidget* m_filterDock = nullptr;
    FilterListModel* m_filterModel = nullptr;
    FindDialog*      m_findDialog = nullptr;

    QAction* m_openAction = nullptr;
    QAction* m_saveFiltersAction = nullptr;
    QAction* m_loadFiltersAction = nullptr;
    QAction* m_exportAction = nullptr;
    QAction* m_findAction = nullptr;

    QLabel* m_pathLabel   = nullptr;
    QLabel* m_rowCountLabel = nullptr;
    QLabel* m_curLineLabel = nullptr;
    QLabel* m_memLabel     = nullptr;
    QLabel* m_statusLabel  = nullptr;   // 操作反馈（右侧，不覆盖路径）
    QTimer* m_statusTimer  = nullptr;

    int  m_nextRuleId = 1;   // [M] 规则 id 分配（删除不复用）
    bool m_applyingRules = false;  // applyCurrentRules 重入守卫

    // R-23：Filters 菜单
    QMenu*   m_viewMenu    = nullptr;   // R-24：挂工具条按钮（单行）
    QMenu*   m_filtersMenu = nullptr;
    QAction* m_editFilterAction   = nullptr;  // 动态启用（有选中行才可用）
    QAction* m_removeFilterAction = nullptr;
    QVector<int> m_matchLines;                // 匹配行缓存（Highlighted）
    int      m_matchPos = 0;
    uint64_t m_matchFingerprint = 0;
    int      m_matchGen = -1;
    QVector<int> m_selectedSearchLines;  // 最近一次搜索命中（F3 循环）
    int m_selectedSearchPos = 0;
};

}  // namespace tat