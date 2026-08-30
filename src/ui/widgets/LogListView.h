// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// LogListView.h: 虚拟化列表视图（百万行滚动）
// 文档：DISPLAYDESIGN.md §7.4（uniformItemSizes / ScrollPerPixel /
//                         快捷键 Ctrl+1..8 Alt+1..8 / 超宽行水平滚动）
// =============================================================================
#pragma once

#include <QListView>

#include "controller/MainController.h"

namespace tat {

class LogViewDelegate;

class LogListView : public QListView {
    Q_OBJECT
public:
    explicit LogListView(MainController* controller, QWidget* parent = nullptr);

    MainController* controller() const noexcept { return m_controller; }

    void scrollToLine(int lineNo);          // 1-based
    int  currentLine() const noexcept { return m_currentLine; }

signals:
    void lineActivated(int lineNo);          // 双击 / Enter
    void markerToggleRequested(int lineNo, int markerId);
    void markerJumpRequested(int lineNo, int markerId);
    void findRequested();                    // Ctrl+F
    void cancelRequested();                  // Esc：取消进行中的任务

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void currentChanged(const QModelIndex& current,
                        const QModelIndex& previous) override;

private:
    void navigate(int direction);  // +1/-1 行移动

    MainController* m_controller;
    int m_currentLine = 1;
};

}  // namespace tat