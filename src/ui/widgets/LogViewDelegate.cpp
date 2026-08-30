// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// LogViewDelegate.cpp: 渲染实现
// 渲染流程见 DISPLAYDESIGN.md §7.3.2；颜色规则 §3.3.1；标记色条 §7.3.6
// =============================================================================

#include "ui/widgets/LogViewDelegate.h"

#include <QPainter>

#include "controller/MainController.h"
#include "ui/widgets/LogViewModel.h"

namespace tat {

LogViewDelegate::LogViewDelegate(MainController* controller, QWidget* parent)
    : QStyledItemDelegate(parent), m_controller(controller) {}

void LogViewDelegate::resolveColors(const RowState& rs, int /*ruleRef*/,
                                    Argb* fg, Argb* bg) const {
    switch (rs.state()) {
    case ResultState::Highlighted: {
        // 颜色按 ruleRef 查规则快照（§3.3.1 契约）
        auto rules = m_controller->rulesSnapshot();
        const int ref = rs.ruleRef();
        if (rules && ref >= 0 && ref < static_cast<int>(rules->rules.size())) {
            const auto& r = rules->rules[static_cast<size_t>(ref)];
            *fg = r.foreground;
            *bg = r.background;
        } else {
            *fg = 0xFF000000;   // 兜底：黑字黄底
            *bg = 0xFFFFFFF0;
        }
        break;
    }
    case ResultState::Dimmed:
        *fg = 0xFF808080;  // 灰色前景，透明背景
        *bg = 0x00000000;
        break;
    case ResultState::Hidden:
        *fg = 0x00000000;  // 不绘制
        *bg = 0x00000000;
        break;
    case ResultState::Normal:
    default:
        *fg = 0xFF000000;  // 默认前景（由 palette 覆盖时仍可读）
        *bg = 0xFFFFFFFF;
        break;
    }
}

void LogViewDelegate::drawLineNumber(QPainter* p,
                                     const QStyleOptionViewItem& opt,
                                     int lineNo) const {
    const QFont f = p->font();
    QFont small = f;
    small.setPointSizeF(f.pointSizeF() * 0.82);
    p->setFont(small);
    p->setPen(QColor(0x8a, 0x8a, 0x8a));
    // 行号列固定在左侧（问题 4）；宽度按行数位数自适应（R-21）
    const int width = m_lineNoWidth;
    const QRect numRect(opt.rect.left() + 2, opt.rect.top(), width - 6,
                        opt.rect.height());
    p->drawText(numRect, Qt::AlignRight | Qt::AlignVCenter,
                QString::number(lineNo));
    p->setFont(f);
}

void LogViewDelegate::drawMarkerBar(QPainter* p,
                                    const QStyleOptionViewItem& opt,
                                    int markerId) const {
    if (markerId < 1 || markerId > kMarkerCount) return;
    p->fillRect(QRect(opt.rect.left(), opt.rect.top(), 4, opt.rect.height()),
                QColor(QRgb(kMarkerArgb[markerId - 1])));
}

void LogViewDelegate::paint(QPainter* p, const QStyleOptionViewItem& option,
                            const QModelIndex& index) const {
    // R-26：视图行经过"可见行映射"，index 携带的 LineNoRole 是原始行号；
    // 隐藏行不出现在视图中（折叠，不占行槽），此处 raw 恒有效。
    const int rawLine = index.data(LogViewModel::LineNoRole).toInt();
    if (rawLine == kInvalidLine) return;
    const int raw = rawLine - 1;

    // 1) 快照（无锁；写者替换不影响已获得的 shared_ptr，§5.4.3）
    auto buffer = m_controller->bufferSnapshot();
    if (!buffer || raw < 0 || raw >= buffer->rowCount()) return;
    auto result = m_controller->resultSnapshot();

    // 2) 文本（缓存 key = 原始行号；视口内命中率高）
    QString text = m_transcodeCache.value(raw);
    if (text.isNull()) {
        const std::string_view sv = buffer->textAt(raw);
        text = QString::fromUtf8(sv.data(), static_cast<qsizetype>(sv.size()));
        if (m_transcodeCache.size() >= 4096) m_transcodeCache.clear();
        m_transcodeCache.insert(raw, text);
    }

    // 3) 状态与颜色
    RowState rs;
    if (result && raw < static_cast<int>(result->states.size()))
        rs.raw = result->states[static_cast<size_t>(raw)];
    Argb fg, bg;
    resolveColors(rs, rs.ruleRef(), &fg, &bg);

    const QRect rect = option.rect.adjusted(0, 0, -1, -1);

    // 4) 隐藏行：映射模型下不可达（折叠）；保留兜底分支仅画行号
    if (rs.isHidden()) {
        drawLineNumber(p, option, rawLine);
        return;
    }

    // 5) 背景（alpha != 0 才绘制）
    if ((bg & 0xFF000000u) != 0) p->fillRect(rect, QColor(QRgb(bg)));
    // 选中：半透明主题蓝叠加（覆盖所有底色，保证与白色背景可区分）
    if (option.state & QStyle::State_Selected)
        p->fillRect(rect, QColor(0x2a, 0x82, 0xda, 0x60));

    // 6) 文本（超宽截断，§7.3.4）；文本区从行号列之后开始
    p->setPen(QColor(QRgb(fg & 0xFFFFFFFFu)));
    if (text.size() > m_maxPaintChars)
        text = text.left(m_maxPaintChars) + QStringLiteral(" …");
    const QRect textRect(option.rect.left() + m_lineNoWidth,
                         option.rect.top(),
                         std::max(8, option.rect.width() - m_lineNoWidth - 6),
                         option.rect.height());
    p->drawText(textRect, Qt::AlignVCenter | Qt::TextSingleLine, text);

    // 7) 标记色条 + 行号（§7.3.6 叠加顺序）
    if (rs.state() != ResultState::Dimmed) {
        const int markerId = m_controller->markers().markerOf(rawLine);
        drawMarkerBar(p, option, markerId);
    }
    drawLineNumber(p, option, rawLine);
}

}  // namespace tat