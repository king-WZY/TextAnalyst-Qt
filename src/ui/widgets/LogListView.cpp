// =============================================================================
// LogListView.cpp
// =============================================================================

#include "ui/widgets/LogListView.h"
#include "ui/widgets/LogViewModel.h"

#include <QKeyEvent>
#include <QScrollBar>

namespace tat {

LogListView::LogListView(MainController* controller, QWidget* parent)
    : QListView(parent), m_controller(controller) {
    // 固定行高：QListView 快速路径，避免逐行 sizeHint（§7.2.2/§7.4.1）
    setUniformItemSizes(true);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

void LogListView::scrollToLine(int rawLine) {
    // R-26：rawLine 是原始行号（1-based）；经模型可见行映射定位——
    // 目标行被隐藏时落在其后的最近可见行
    if (rawLine < 1) return;
    auto* m = qobject_cast<LogViewModel*>(QListView::model());
    if (!m) return;
    const int viewRow = m->visibleIndexFor(rawLine);
    if (viewRow < 0 || viewRow >= m->rowCount()) return;
    m_currentLine = rawLine;
    const QModelIndex idx = m->index(viewRow, 0);
    scrollTo(idx, QAbstractItemView::PositionAtCenter);
    setCurrentIndex(idx);
}

void LogListView::currentChanged(const QModelIndex& current,
                                 const QModelIndex& /*previous*/) {
    if (current.isValid()) {
        // R-26：currentLine 语义 = 原始行号（隐藏行折叠后行号仍连续定位）
        m_currentLine = current.data(LogViewModel::LineNoRole).toInt();
    }
}

void LogListView::navigate(int direction) {
    auto* m = qobject_cast<LogViewModel*>(QListView::model());
    if (!m || m->rowCount() == 0) return;
    // 视图行推进（可见行之间移动），换算回原始行号
    const int curViewRow = m->visibleIndexFor(m_currentLine);
    const int targetViewRow = std::clamp(curViewRow + direction, 0,
                                         m->rowCount() - 1);
    scrollToLine(m->rawLineOf(targetViewRow));
}

void LogListView::keyPressEvent(QKeyEvent* event) {
    // Ctrl+1..8 切换标记；Alt+1..8 跳转标记（§7.4.2）
    const int key = event->key();
    if ((event->modifiers() & Qt::ControlModifier) && key >= Qt::Key_1 &&
        key <= Qt::Key_8) {
        emit markerToggleRequested(m_currentLine, key - Qt::Key_1 + 1);
        return;
    }
    if ((event->modifiers() & Qt::AltModifier) && key >= Qt::Key_1 &&
        key <= Qt::Key_8) {
        emit markerJumpRequested(m_currentLine, key - Qt::Key_1 + 1);
        return;
    }
    switch (key) {
    case Qt::Key_Down:
        navigate(+1);
        return;
    case Qt::Key_Up:
        navigate(-1);
        return;
    case Qt::Key_PageDown:
        navigate(verticalScrollBar()->pageStep());
        return;
    case Qt::Key_PageUp:
        navigate(-verticalScrollBar()->pageStep());
        return;
    case Qt::Key_Home:
        if (event->modifiers() & Qt::ControlModifier) {
            scrollToLine(1);  // README 承诺：Ctrl+Home 跳首行
            return;
        }
        break;
    case Qt::Key_End:
        if (event->modifiers() & Qt::ControlModifier) {
            auto* m = qobject_cast<LogViewModel*>(QListView::model());
            if (m && m->rowCount() > 0)
                scrollToLine(m->rawLineOf(m->rowCount() - 1));  // 最后可见行
            return;
        }
        break;
    case Qt::Key_Escape:
        emit cancelRequested();  // README 承诺：Esc 取消运行中的过滤/搜索
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit lineActivated(m_currentLine);
        return;
    default:
        break;
    }
    if ((event->modifiers() & Qt::ControlModifier) && key == Qt::Key_F) {
        emit findRequested();
        return;
    }
    QListView::keyPressEvent(event);
}

void LogListView::mouseDoubleClickEvent(QMouseEvent* event) {
    const QModelIndex idx = indexAt(event->pos());
    if (idx.isValid()) {
        m_currentLine = idx.row() + 1;
        emit lineActivated(m_currentLine);
        return;
    }
    QListView::mouseDoubleClickEvent(event);
}

}  // namespace tat