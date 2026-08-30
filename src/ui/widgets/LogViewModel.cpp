// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// LogViewModel.cpp: 可见行映射实现（R-26）
// =============================================================================

#include "ui/widgets/LogViewModel.h"

#include <QFontMetrics>

#include <algorithm>

namespace tat {

LogViewModel::LogViewModel(MainController* controller, QObject* parent)
    : QAbstractListModel(parent), m_controller(controller) {
    m_font = QFont(QStringLiteral("monospace"), 10);
    setFont(m_font);
}

void LogViewModel::setFont(const QFont& f) {
    m_font = f;
    QFontMetrics fm(f);
    m_rowHeight = fm.height() + 2;
}

void LogViewModel::rebuildFromController() {
    beginResetModel();
    m_visibleRows.clear();
    m_identity = true;

    if (m_controller) {
        auto buffer = m_controller->bufferSnapshot();
        auto result = m_controller->resultSnapshot();
        if (buffer) {
            const int n = buffer->rowCount();
            const bool hasResult = result != nullptr;
            if (m_showMatchedOnly || hasResult) {
                // 存在过滤结果或仅显示模式 → 构建可见行映射
                m_identity = false;
                m_visibleRows.reserve(static_cast<size_t>(n));
                for (int i = 0; i < n; ++i) {
                    RowState rs;
                    if (result && i < static_cast<int>(result->states.size()))
                        rs.raw = result->states[static_cast<size_t>(i)];
                    const bool visible = m_showMatchedOnly
                                             ? rs.state() == ResultState::Highlighted
                                             : !rs.isHidden();
                    if (visible)
                        m_visibleRows.push_back(i + 1);  // 原始行号
                }
                // 全部可见 → 退回恒等（零开销）
                if (static_cast<int>(m_visibleRows.size()) == n &&
                    !m_showMatchedOnly) {
                    m_identity = true;
                    m_visibleRows.clear();
                }
            }
        }
    }
    endResetModel();
}

void LogViewModel::setShowMatchedOnly(bool on) {
    if (m_showMatchedOnly == on) return;
    m_showMatchedOnly = on;
    rebuildFromController();
}

int LogViewModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    if (m_identity) {
        return m_controller ? m_controller->rowCount() : 0;
    }
    return static_cast<int>(m_visibleRows.size());
}

int LogViewModel::rawLineOf(int viewRow) const noexcept {
    if (m_identity) return viewRow + 1;  // 视图行 0-based → 原始 1-based
    if (viewRow < 0 || viewRow >= static_cast<int>(m_visibleRows.size()))
        return kInvalidLine;
    return m_visibleRows[static_cast<size_t>(viewRow)];
}

int LogViewModel::visibleIndexFor(int rawLine) const noexcept {
    if (m_identity) return rawLine - 1;
    // 升序表二分：原始行被隐藏时落在其后的最近可见行
    const auto it = std::lower_bound(m_visibleRows.begin(),
                                     m_visibleRows.end(), rawLine);
    if (it == m_visibleRows.end()) {
        return m_visibleRows.empty()
                   ? -1
                   : static_cast<int>(m_visibleRows.size()) - 1;
    }
    return static_cast<int>(it - m_visibleRows.begin());
}

QVariant LogViewModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || !m_controller) return {};
    const int rawLine = rawLineOf(index.row());
    if (rawLine == kInvalidLine) return {};
    const int raw = rawLine - 1;  // 0-based 原始索引

    switch (role) {
    case Qt::DisplayRole:
    case TextRole: {
        const std::string_view sv = m_controller->bufferSnapshot()
                                        ? m_controller->bufferSnapshot()->textAt(raw)
                                        : std::string_view{};
        return QString::fromUtf8(sv.data(), static_cast<qsizetype>(sv.size()));
    }
    case LineNoRole:
        return rawLine;  // 原始行号（1-based）
    case StateRole: {
        auto result = m_controller->resultSnapshot();
        if (!result || raw >= static_cast<int>(result->states.size())) return 0;
        return static_cast<int>(result->states[static_cast<size_t>(raw)] & 0x3);
    }
    case RuleRefRole: {
        auto result = m_controller->resultSnapshot();
        if (!result || raw >= static_cast<int>(result->states.size())) return 0;
        return static_cast<int>(result->states[static_cast<size_t>(raw)] >> 2);
    }
    default:
        return {};
    }
}

}  // namespace tat