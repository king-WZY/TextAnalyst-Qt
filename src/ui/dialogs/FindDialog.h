// =============================================================================
// FindDialog.h: 搜索对话框（搜索框 + 选项 + 结果计数）
// 文档：DISPLAYDESIGN.md §7.7
// =============================================================================
#pragma once

#include <QDialog>

#include "core/engine/Searcher.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

namespace tat {

class FindDialog : public QDialog {
    Q_OBJECT
public:
    explicit FindDialog(QWidget* parent = nullptr);

    SearchOptions options() const;

signals:
    void searchRequested(const SearchOptions& opt);
    // 增量搜索（输入 200ms 防抖后触发；仅 Substring，视口 ±500 行）
    void incrementalSearchRequested(const SearchOptions& opt);
    void nextHitRequested(int direction);   // +1 下一个 / -1 上一个（F3 循环）
    void cancelRequested();

public slots:
    void onSearchCompleted(const SearchResult& result);  // 全量："找到 N 处"
    void onIncrementalResult(int viewportCount);         // 增量："视口内 N 处"

private slots:
    void onFindClicked();
    void onPatternEdited(const QString& text);  // 启动防抖

private:
    QTimer*    m_debounce = nullptr;   // 200 ms（§7.5.2 同款 MUST QTimer）
    QLineEdit* m_patternEdit = nullptr;
    QCheckBox* m_caseCheck = nullptr;
    QCheckBox* m_regexCheck = nullptr;
    QCheckBox* m_wholeWordCheck = nullptr;
    QLabel*    m_resultLabel = nullptr;
    QPushButton* m_findButton = nullptr;
};

}  // namespace tat