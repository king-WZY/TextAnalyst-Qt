// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// LogViewDelegate.h: 渲染委托（paint 只做查表 + 绘图，绝不正则/I/O）
// 文档：DISPLAYDESIGN.md §7.3（颜色解析/超宽行截断/隐藏行行槽/标记色条）
// 线程安全：paint [M]；快照经 MainController 无锁获取
// =============================================================================
#pragma once

#include <QHash>
#include <QStyledItemDelegate>

#include "core/models/RowState.h"
#include "core/models/common.h"

namespace tat {

class MainController;
class LogViewModel;

class LogViewDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit LogViewDelegate(MainController* controller,
                             QWidget* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

public slots:
    // 换文件时由 MainWindow 调用（bufferChanged）：转码缓存按行号为 key，
    // 不清空会让新文件命中旧文件的文本缓存（"追加"观感的根因）
    void clearTranscodeCache() { m_transcodeCache.clear(); }

private:
    void resolveColors(const RowState& rs, int ruleRef,
                       Argb* fg, Argb* bg) const;
    void drawLineNumber(QPainter* p, const QStyleOptionViewItem& opt,
                        int lineNo) const;
    void drawMarkerBar(QPainter* p, const QStyleOptionViewItem& opt,
                       int markerId) const;

public:
    // 行号列宽随总行数位数自适应（MainWindow 在文件打开后调用）
    void setLineNoWidth(int width) { m_lineNoWidth = width; }

    MainController* m_controller;
    // 转码缓存（LRU 清空式淘汰，容量 4096，§7.3.3）
    mutable QHash<int, QString> m_transcodeCache;
    int m_maxPaintChars = 16384;   // 单行最大绘制字符（§7.3.4）
    int m_lineNoWidth = 64;        // 行号列宽（R-21：按行数位数自适应）
};

}  // namespace tat