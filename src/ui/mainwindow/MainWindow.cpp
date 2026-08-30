// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// MainWindow.cpp: 主窗口装配与信号路由（§7.6.1）
// =============================================================================

#include "ui/mainwindow/MainWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QStatusBar>
#include <QTableView>
#include <QStyleFactory>
#include <QToolBar>
#include <QMenu>
#include <QToolButton>
#include <QTimer>

#include "core/engine/ResultStore.h"
#include "core/engine/Searcher.h"
#include "ui/dialogs/FilterRuleDialog.h"
#include "ui/dialogs/FindDialog.h"
#include "ui/widgets/FilterDockWidget.h"
#include "ui/widgets/FilterListModel.h"
#include "ui/widgets/LogViewDelegate.h"
#include "ui/widgets/LogViewModel.h"
#include "ui/widgets/LogListView.h"
#include "io/SettingsManager.h"

namespace tat {

MainWindow::MainWindow(MainController* controller, SettingsManager* settings,
                       QWidget* parent)
    : QMainWindow(parent), m_controller(controller), m_settings(settings) {
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    setWindowTitle(QStringLiteral("TextAnalyst-Qt"));
    resize(1200, 800);

    // ---- 中央视图 ----
    m_model = new LogViewModel(m_controller, this);
    m_delegate = new LogViewDelegate(m_controller, this);
    m_view = new LogListView(m_controller, this);
    m_view->setModel(m_model);
    m_view->setItemDelegate(m_delegate);
    setCentralWidget(m_view);

    // ---- 过滤器面板：底部呈现（与日志视图构成上下两部分，问题 3/5）----
    m_filterModel = new FilterListModel(this);
    m_filterDock = new FilterDockWidget(this);
    m_filterDock->setObjectName("filterDock");
    m_filterDock->setModel(m_filterModel);
    addDockWidget(Qt::BottomDockWidgetArea, m_filterDock);
    resizeDocks({m_filterDock}, {280}, Qt::Vertical);

    buildMenus();      // 先建菜单对象（R-24：单行工具条挂菜单按钮）
    setupActions();
    setupStatusBar();

    // ---- 信号路由（§6.3 命令路由表）----
    connect(m_controller, &MainController::fileOpened, this,
            &MainWindow::onFileOpened);
    connect(m_controller, &MainController::fileOpenFailed, this,
            &MainWindow::onFileOpenFailed);
    connect(m_controller, &MainController::filterApplied, this,
            &MainWindow::onFilterApplied);
    connect(m_controller, &MainController::currentLineChanged, this,
            [this](int line) { m_curLineLabel->setText(tr("行 %1").arg(line)); });

    connect(m_view, &LogListView::lineActivated, this,
            &MainWindow::onLineDoubleClicked);
    connect(m_view, &LogListView::markerToggleRequested, m_controller,
            [this](int line, int id) {
                const bool added = m_controller->toggleMarker(line, id);
                statusBar()->showMessage(
                    added ? tr("已标记 #%1 @行 %2").arg(id).arg(line)
                          : tr("已取消标记 #%1 @行 %2").arg(id).arg(line),
                    2000);
            });
    connect(m_view, &LogListView::markerJumpRequested, m_controller,
            [this](int line, int id) {
                if (m_controller->jumpToNextMarker(line, id))
                    m_view->scrollToLine(m_controller->currentLine());
            });
    connect(m_view, &LogListView::findRequested, this,
            &MainWindow::onFindTriggered);
    connect(m_controller, &MainController::statusMessage, this,
            [this](const QString& msg) { status(msg); });
    // 换文件（bufferChanged）：清空委托转码缓存与搜索命中缓存——
    // 缓存按行号为 key，不清空会让新文件的行命中旧文件的文本，
    // 观感上"新文件追加在旧文件后面"（用户反馈问题 2 的根因）
    connect(m_controller, &MainController::bufferChanged, m_delegate,
            &LogViewDelegate::clearTranscodeCache);
    connect(m_controller, &MainController::bufferChanged, m_model,
            &LogViewModel::rebuildFromController);   // R-26：重建可见行映射
    connect(m_controller, &MainController::bufferChanged, this, [this] {
        m_selectedSearchLines.clear();
        m_selectedSearchPos = 0;
        m_matchLines.clear();   // F8 匹配缓存同样失效
        m_view->clearSelection();
        m_view->scrollToLine(1);
    });
    connect(m_filterDock, &FilterDockWidget::rulesListChanged, this,
            &MainWindow::onRulesListChanged);
    connect(m_filterDock, &FilterDockWidget::ruleDoubleClicked, this,
            &MainWindow::onEditRuleRequested);

    // ---- 会话恢复 ----
    if (m_settings) {
        const QByteArray geo = m_settings->geometry();
        if (!geo.isEmpty()) restoreGeometry(geo);
        // R-20：布局状态版本化。旧版本（右侧 dock 时代）保存的 state 没有
        // "v2|" 前缀，直接丢弃——否则启动时恢复旧布局，底部新布局被覆盖，
        // 用户会看到"问题 3 未修复"。
        const QByteArray state = m_settings->dockState();
        const QByteArray kPrefix = QByteArrayLiteral("v2|");
        if (state.startsWith(kPrefix)) {
            restoreState(state.mid(kPrefix.size()));
        }
    }

    // 延迟创建搜索对话框（Ctrl+F 时）
    m_findDialog = nullptr;
}

void MainWindow::setupActions() {
    QToolBar* tb = addToolBar(QStringLiteral("工具条"));
    tb->setObjectName("mainToolbar");
    tb->setMovable(false);

    // 顶部单行（R-25 排序）：
    // [打开][保存规则][加载规则] │ [视图▾][Filters▾] │ [导出可见][查找]
    m_openAction = tb->addAction(QStringLiteral("打开"));
    connect(m_openAction, &QAction::triggered, this,
            &MainWindow::onOpenTriggered);
    m_saveFiltersAction = tb->addAction(QStringLiteral("保存规则"));
    connect(m_saveFiltersAction, &QAction::triggered, this,
            &MainWindow::onSaveFiltersTriggered);
    m_loadFiltersAction = tb->addAction(QStringLiteral("加载规则"));
    connect(m_loadFiltersAction, &QAction::triggered, this,
            &MainWindow::onLoadFiltersTriggered);

    tb->addSeparator();

    auto* viewBtn = new QToolButton(tb);
    viewBtn->setText(QStringLiteral("视图"));
    viewBtn->setPopupMode(QToolButton::InstantPopup);
    viewBtn->setMenu(m_viewMenu);
    tb->addWidget(viewBtn);

    auto* filtersBtn = new QToolButton(tb);
    filtersBtn->setText(QStringLiteral("Filters"));
    filtersBtn->setPopupMode(QToolButton::InstantPopup);
    filtersBtn->setMenu(m_filtersMenu);
    tb->addWidget(filtersBtn);

    tb->addSeparator();

    m_exportAction = tb->addAction(QStringLiteral("导出可见"));
    connect(m_exportAction, &QAction::triggered, this,
            &MainWindow::onExportTriggered);
    m_findAction = tb->addAction(QStringLiteral("查找"));
    m_findAction->setShortcut(QKeySequence::Find);
    connect(m_findAction, &QAction::triggered, this,
            &MainWindow::onFindTriggered);
}

void MainWindow::buildMenus() {
    // R-23/R-24：视图 / Filters 菜单对象创建；挂载点为工具条上的菜单
    // 按钮（顶部单行），不再使用独立菜单栏（避免两行标题结构）。
    m_viewMenu = new QMenu(QStringLiteral("视图(&V)"), this);
    m_viewMenu->addAction(m_filterDock->toggleViewAction());  // 过滤面板可关闭/恢复
    // R-26c：仅显示过滤的行（TAT.NET 的 Ctrl+H）
    QAction* showMatchedOnly =
        m_viewMenu->addAction(QStringLiteral("仅显示过滤的行"));
    showMatchedOnly->setCheckable(true);
    showMatchedOnly->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));
    connect(showMatchedOnly, &QAction::toggled, this, [this](bool on) {
        if (m_model) {
            m_model->setShowMatchedOnly(on);
            status(on ? tr("仅显示过滤的行") : tr("显示全部行"));
        }
    });

    QMenu* menu = m_filtersMenu = new QMenu(QStringLiteral("&Filters"), this);

    QAction* prevMatch = menu->addAction(QStringLiteral("Previous Match"));
    prevMatch->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F8));
    connect(prevMatch, &QAction::triggered, this,
            [this] { jumpToMatch(-1); });

    QAction* nextMatch = menu->addAction(QStringLiteral("Next Match"));
    nextMatch->setShortcut(QKeySequence(Qt::Key_F8));
    connect(nextMatch, &QAction::triggered, this,
            [this] { jumpToMatch(+1); });

    menu->addSeparator();

    QAction* addFilter = menu->addAction(QStringLiteral("Add New Filter..."));
    addFilter->setShortcut(QKeySequence::New);  // Ctrl+N
    connect(addFilter, &QAction::triggered, this,
            &MainWindow::onAddRuleRequested);

    m_editFilterAction = menu->addAction(QStringLiteral("Edit Selected Filter..."));
    connect(m_editFilterAction, &QAction::triggered, this, [this] {
        const int row = selectedRuleRow();
        if (row >= 0) onEditRuleRequested(row);
    });

    m_removeFilterAction = menu->addAction(QStringLiteral("Remove Selected Filter"));
    connect(m_removeFilterAction, &QAction::triggered, this, [this] {
        const int row = selectedRuleRow();
        if (row >= 0) onRemoveRuleRequested(row);
    });

    menu->addSeparator();

    QAction* enableAll = menu->addAction(QStringLiteral("Enable All Filters"));
    connect(enableAll, &QAction::triggered, this,
            [this] { onEnableAllFilters(true); });

    QAction* disableAll = menu->addAction(QStringLiteral("Disable All Filters"));
    connect(disableAll, &QAction::triggered, this,
            [this] { onEnableAllFilters(false); });

    QAction* removeAll = menu->addAction(QStringLiteral("Remove All Filters"));
    connect(removeAll, &QAction::triggered, this,
            &MainWindow::onRemoveAllFilters);

    // 菜单打开时：Edit/Remove 依"是否有选中规则行"动态可用
    connect(menu, &QMenu::aboutToShow, this, [this] {
        const bool hasSelection = selectedRuleRow() >= 0;
        m_editFilterAction->setEnabled(hasSelection);
        m_removeFilterAction->setEnabled(hasSelection);
    });
}

int MainWindow::selectedRuleRow() const {
    if (!m_filterDock || !m_filterDock->tableView()) return -1;
    const QModelIndex idx = m_filterDock->tableView()->currentIndex();
    return idx.isValid() ? idx.row() : -1;
}

void MainWindow::jumpToMatch(int direction) {
    // F8/Shift+F8：在"着色规则命中"的行之间循环跳转。
    // 缓存按 (规则指纹, 结果世代) 失效，规则变更后自动重建。
    auto result = m_controller->resultSnapshot();
    auto rules = m_controller->rulesSnapshot();
    if (!rules || !rules->anyRule) {
        status(tr("没有过滤规则"), 2000);
        return;
    }
    if (!result || result->ruleFingerprint != m_matchFingerprint ||
        result->generation != m_matchGen) {
        m_matchLines.clear();
        if (result) {
            for (int i = 0; i < static_cast<int>(result->states.size()); ++i) {
                RowState rs;
                rs.raw = result->states[static_cast<size_t>(i)];
                if (rs.state() == ResultState::Highlighted)
                    m_matchLines.append(i + 1);
            }
        }
        m_matchFingerprint = rules->fingerprint;
        m_matchGen = result ? result->generation : -1;
        m_matchPos = 0;
    }
    if (m_matchLines.isEmpty()) {
        status(tr("没有匹配的行"), 2000);
        return;
    }
    const int n = m_matchLines.size();
    m_matchPos = ((m_matchPos + direction) % n + n) % n;
    m_view->scrollToLine(m_matchLines.at(m_matchPos));
    status(tr("匹配 %1 / %2").arg(m_matchPos + 1).arg(n));
}

void MainWindow::onEnableAllFilters(bool enabled) {
    if (!m_filterModel) return;
    m_filterModel->enableAll(enabled);
    status(enabled ? tr("已启用全部过滤规则") : tr("已禁用全部过滤规则"));
}

void MainWindow::onRemoveAllFilters() {
    if (!m_filterModel) return;
    if (m_filterModel->rowCount() == 0) return;
    m_filterModel->clearRules();
    status(tr("已清除全部过滤规则"));
}

void MainWindow::setupStatusBar() {
    // 左侧：文件完整路径（常驻，不再被临时消息覆盖，问题 6）
    m_pathLabel = new QLabel(QStringLiteral("未打开文件"), this);
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_rowCountLabel = new QLabel(QStringLiteral("0 行"), this);
    m_curLineLabel = new QLabel(QStringLiteral("行 1"), this);
    m_memLabel = new QLabel(QStringLiteral("内存: -"), this);
    m_statusLabel = new QLabel(this);
    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    connect(m_statusTimer, &QTimer::timeout, m_statusLabel,
            &QLabel::clear);

    statusBar()->addWidget(m_pathLabel, 1);
    statusBar()->addPermanentWidget(m_statusLabel);
    statusBar()->addPermanentWidget(m_rowCountLabel);
    statusBar()->addPermanentWidget(m_curLineLabel);
    statusBar()->addPermanentWidget(m_memLabel);
}

void MainWindow::status(const QString& message, int timeoutMs) {
    if (!m_statusLabel) return;
    m_statusLabel->setText(message);
    m_statusTimer->start(timeoutMs);
}

void MainWindow::openFile(const QString& path) {
    m_controller->openFile(path);
}

void MainWindow::onFileOpened(const QString& path, int rowCount) {
    m_pathLabel->setText(QFileInfo(path).absoluteFilePath());
    m_pathLabel->setToolTip(QFileInfo(path).absoluteFilePath());
    m_rowCountLabel->setText(tr("%1 行").arg(rowCount));
    // 行号列宽自适应：位数 × 单字符宽 + 余量，clamp 48..112（R-21）
    if (m_delegate) {
        const int digits = QString::number(rowCount).size();
        const int charW =
            QFontMetrics(m_view->font()).horizontalAdvance(QLatin1Char('0'));
        m_delegate->setLineNoWidth(
            std::clamp(digits * charW + 18, 48, 112));
        m_view->doItemsLayout();
    }
    m_controller->jumpToLine(1);
    m_view->scrollToLine(1);
    m_filterModel->clearRules();
    m_controller->applyFilter({});  // 新文件：清空过滤状态
    updateStatusBar();
    status(tr("已打开 %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::onFileOpenFailed(const QString& path, const QString& op,
                                  const QString& message) {
    status(tr("打开失败 %1: %2: %3").arg(QFileInfo(path).fileName(), op,
                                         message),
           8000);
}

void MainWindow::onFilterApplied(bool ok, const QString& message) {
    // 取消（用户修改规则触发新任务）不算失败，静默处理
    if (!ok && !message.contains(QLatin1String("cancelled")))
        status(tr("过滤失败: %1").arg(message), 5000);
    auto result = m_controller->resultSnapshot();
    if (result) m_filterModel->refreshMatchCounts(*result, {});
    if (m_model) m_model->rebuildFromController();  // R-26：Hidden 行折叠生效
    updateStatusBar();
}

void MainWindow::onOpenTriggered() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开日志文件"), QString(),
        QStringLiteral("文本/日志 (*.log *.txt *);;所有文件 (*)"));
    if (!path.isEmpty()) openFile(path);
}

void MainWindow::onSaveFiltersTriggered() {
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存过滤规则"), QString(),
        QStringLiteral("TAT 过滤器 (*.tat)"));
    if (path.isEmpty()) return;
    // 自动追加 .tat 后缀：getSaveFileName 在 Linux 上不自动补全，
    // 缺后缀的文件会被加载对话框的 *.tat 过滤器隐藏（用户反馈）
    if (!path.endsWith(QStringLiteral(".tat"), Qt::CaseInsensitive))
        path += QStringLiteral(".tat");
    std::vector<FilterRule> rules;
    collectRules(&rules);
    const Error e = m_controller->saveFilters(path, rules);
    status(e.ok() ? tr("规则已保存到 %1").arg(QFileInfo(path).fileName())
                  : tr("规则保存失败: %1").arg(QString::fromStdString(e.message)));
}

void MainWindow::onLoadFiltersTriggered() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("加载过滤规则"), QString(),
        QStringLiteral("TAT 过滤器 (*.tat);;所有文件 (*)"));
    if (path.isEmpty()) return;
    std::vector<FilterRule> rules;
    const Error e = m_controller->loadFilters(path, &rules);
    if (!e.ok()) {
        status(tr("规则加载失败: %1").arg(QString::fromStdString(e.message)), 6000);
        return;
    }
    status(tr("已从 %1 加载 %2 条规则").arg(QFileInfo(path).fileName()).arg(rules.size()));
    m_filterModel->clearRules();
    for (auto& r : rules) {
        r.matchCount = 0;
        m_filterModel->addRule(r);
    }
    applyCurrentRules();
}

void MainWindow::onExportTriggered() {
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出"), QStringLiteral("export.txt"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive))
        path += QStringLiteral(".txt");  // 与保存规则同款后缀补全
    auto buffer = m_controller->bufferSnapshot();
    if (!buffer) return;
    // 导出语义与 --export 一致：跳过 Hidden/Dimmed（控制层统一实现）
    const Error e = m_controller->exportLines(1, buffer->rowCount(), path,
                                              /*skipDimmed=*/true);
    status(e.ok() ? tr("已导出 %1").arg(QFileInfo(path).fileName())
                  : tr("导出失败: %1").arg(QString::fromStdString(e.message)));
}

void MainWindow::onFindTriggered() {
    if (!m_findDialog) {
        m_findDialog = new FindDialog(this);
        connect(m_findDialog, &FindDialog::searchRequested, this,
                &MainWindow::onSearchRequested);
        connect(m_findDialog, &FindDialog::incrementalSearchRequested, this,
                &MainWindow::onIncrementalSearch);
        connect(m_findDialog, &FindDialog::nextHitRequested, this,
                [this](int dir) { jumpToSearchHit(dir); });
        connect(m_controller, &MainController::searchCompleted, m_findDialog,
                &FindDialog::onSearchCompleted);
        connect(m_controller, &MainController::searchCompleted, this,
                &MainWindow::onSearchCompleted);
    }
    m_findDialog->show();
    m_findDialog->raise();
    m_findDialog->activateWindow();
}

void MainWindow::onSearchRequested(const SearchOptions& opt) {
    m_controller->search(opt);
}

void MainWindow::onIncrementalSearch(const SearchOptions& opt) {
    // 增量搜索：当前视口 ±500 行，主线程即时执行（Substring 快速路径，
    // 千行级耗时 <1 ms，不违反"主线程禁正则/重 I/O"约束）
    auto buffer = m_controller->bufferSnapshot();
    if (!buffer || !m_findDialog) return;
    std::vector<SearchHit> hits;
    const Error e = Searcher::searchViewport(*buffer, opt,
                                             m_controller->currentLine(), 500,
                                             MatchFn{}, &hits);
    m_findDialog->onIncrementalResult(e.ok() ? static_cast<int>(hits.size())
                                            : -1);
}

void MainWindow::onSearchCompleted(const SearchResult& result) {
    m_selectedSearchLines.clear();
    for (const auto& h : result.hits)
        m_selectedSearchLines.append(h.line);
    m_selectedSearchPos = 0;
    if (!m_selectedSearchLines.isEmpty()) {
        m_view->scrollToLine(m_selectedSearchLines.first());
        status(tr("找到 %1 处").arg(m_selectedSearchLines.size()));
    } else {
        status(tr("无匹配"), 2000);
    }
}

void MainWindow::jumpToSearchHit(int direction) {
    if (m_selectedSearchLines.isEmpty()) return;
    const int n = m_selectedSearchLines.size();
    m_selectedSearchPos =
        ((m_selectedSearchPos + direction) % n + n) % n;  // 循环
    m_view->scrollToLine(m_selectedSearchLines.at(m_selectedSearchPos));
    status(tr("命中 %1 / %2").arg(m_selectedSearchPos + 1).arg(n));
}

void MainWindow::onRulesListChanged() { applyCurrentRules(); }

void MainWindow::onLineDoubleClicked(int lineNo) {
    auto buffer = m_controller->bufferSnapshot();
    if (!buffer || lineNo < 1 || lineNo > buffer->rowCount()) return;
    // R-22：双击日志行 → 弹出"添加过滤规则"弹窗，预填该行文本；
    // 确认后弹窗销毁、规则入列表并应用；取消则不产生任何规则。
    FilterRule init;
    init.pattern = buffer->toUtf8(lineNo - 1);
    openRuleDialog(init, -1);
}

void MainWindow::onAddRuleRequested() {
    openRuleDialog(FilterRule{}, -1);  // 空表单添加
}

void MainWindow::onEditRuleRequested(int row) {
    if (!m_filterModel || row < 0 || row >= m_filterModel->rowCount()) return;
    // R-22：双击规则行 → 同一弹窗的"修改"模式（预填现有规则）
    openRuleDialog(m_filterModel->ruleAt(row),
                   m_filterModel->ruleAt(row).id);
}

void MainWindow::onRemoveRuleRequested(int row) {
    if (!m_filterModel || row < 0) return;
    m_filterModel->removeRuleAt(row);
    status(tr("已删除规则"));
}

void MainWindow::openRuleDialog(const FilterRule& init, int editId) {
    // 窗口模态（open()）；finished（accept/reject/close 任一结束路径）
    // → deleteLater：确认或取消后弹窗销毁，不占空间（R-22）。
    // 注意：不依赖 WA_DeleteOnClose——open() 后 close() 在部分平台
    // （如 offscreen）不触发该属性删除。
    auto* dlg = new FilterRuleDialog(this);
    connect(dlg, &QDialog::finished, dlg, &QObject::deleteLater);
    dlg->setRule(init);
    if (editId >= 0) dlg->setEditMode(editId);
    connect(dlg, &QDialog::accepted, this, [this, dlg, editId] {
        FilterRule r = dlg->rule();
        if (r.pattern.empty()) return;
        if (editId >= 0) {
            // 修改模式：按行号回写（面板表格当前选中即来源行）
            const QModelIndex idx = m_filterDock->tableView()->currentIndex();
            if (idx.isValid()) m_filterModel->replaceRuleAt(idx.row(), r);
            status(tr("规则 #%1 已更新").arg(editId));
        } else {
            r.id = m_nextRuleId++;
            r.rank = m_filterModel->rowCount();
            m_filterModel->addRule(r);
            status(tr("已添加规则: %1")
                       .arg(QString::fromStdString(r.pattern)));
        }
        applyCurrentRules();
    });
    dlg->open();
}

void MainWindow::collectRules(std::vector<FilterRule>* out) const {
    out->clear();
    const QVector<FilterRule> rules = m_filterModel->rules();
    for (const auto& r : rules) out->push_back(r);
}

void MainWindow::applyCurrentRules() {
    if (m_applyingRules) return;  // id 回写触发 dataChanged → 防重入
    m_applyingRules = true;
    std::vector<FilterRule> rules;
    collectRules(&rules);
    for (auto& r : rules) {
        if (r.id <= 0) r.id = m_nextRuleId++;  // 新添加的规则分配稳定 id
    }
    // 回写 id 到模型（规则跳转/计数按 id 索引）
    if (m_filterModel) {
        const QVector<FilterRule> modelRules = m_filterModel->rules();
        for (int i = 0; i < modelRules.size() && i < (int)rules.size(); ++i) {
            if (modelRules[i].id != rules[(size_t)i].id)
                m_filterModel->replaceRuleIdAt(i, rules[(size_t)i].id);
        }
    }
    m_controller->applyFilter(rules);
    m_applyingRules = false;
}

void MainWindow::updateStatusBar() {
    const auto fp = m_controller->memoryFootprint();
    m_memLabel->setText(
        tr("内存: %1 MB").arg(fp.resident / 1048576.0, 0, 'f', 0));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_settings) {
        m_settings->setGeometry(saveGeometry());
        m_settings->setDockState(QByteArrayLiteral("v2|") + saveState());
    }
    m_controller->saveSession();
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (!urls.isEmpty()) openFile(urls.first().toLocalFile());
}

}  // namespace tat