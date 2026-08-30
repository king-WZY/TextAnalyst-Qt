// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// TatSerializer.cpp: .tat XML 格式实现
// 既有属性（保持原版 TAT 兼容）：id/foreColor/backColor/pattern/isInclude/
//   matchMode/caseSensitive/isEnabled
// 扩展可选属性（v1.0）：word（词边界）/rank（显示顺序）
// =============================================================================

#include "io/TatSerializer.h"

#include <QFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace tat {

QString TatSerializer::colorToHex(Argb argb) {
    // Argb 是 0xAARRGGBB；原版 TAT 颜色为 #RRGGBB（无 alpha）
    return QString("#%1").arg((argb & 0x00FFFFFFu), 6, 16, QLatin1Char('0'));
}

bool TatSerializer::hexToColor(const QString& hex, Argb* out) {
    QString h = hex.trimmed();
    if (h.startsWith('#')) h = h.mid(1);
    if (h.size() != 6) return false;
    bool ok = false;
    const uint32_t rgb = h.toUInt(&ok, 16);
    if (!ok) return false;
    *out = 0xFF000000u | rgb;  // alpha 固定为 FF
    return true;
}

QString TatSerializer::actToStr(FilterAction a) {
    return a == FilterAction::Include ? "1" : "0";  // isInclude 语义
}
QString TatSerializer::modeToStr(MatchMode m) {
    return m == MatchMode::Regex ? "Regex" : "Substring";
}
QString TatSerializer::matchToStr(FilterMatch m) {
    switch (m) {
    case FilterMatch::Exact:      return "Exact";
    case FilterMatch::StartsWith: return "StartsWith";
    case FilterMatch::EndsWith:   return "EndsWith";
    case FilterMatch::Contains:
    default:                      return "Contains";
    }
}
bool TatSerializer::strToMatch(const QString& s, FilterMatch* out) {
    if (s.isEmpty() || s.compare("Contains", Qt::CaseInsensitive) == 0) {
        *out = FilterMatch::Contains; return true;   // 缺省值向后兼容
    }
    if (s.compare("Exact", Qt::CaseInsensitive) == 0) { *out = FilterMatch::Exact; return true; }
    if (s.compare("StartsWith", Qt::CaseInsensitive) == 0) { *out = FilterMatch::StartsWith; return true; }
    if (s.compare("EndsWith", Qt::CaseInsensitive) == 0) { *out = FilterMatch::EndsWith; return true; }
    return false;
}
bool TatSerializer::strToAct(const QString& s, FilterAction* out) {
    if (s == "1" || s.compare("true", Qt::CaseInsensitive) == 0) {
        *out = FilterAction::Include; return true;
    }
    if (s == "0" || s.compare("false", Qt::CaseInsensitive) == 0) {
        *out = FilterAction::Exclude; return true;
    }
    return false;
}
bool TatSerializer::strToMode(const QString& s, MatchMode* out) {
    if (s.compare("Regex", Qt::CaseInsensitive) == 0) { *out = MatchMode::Regex; return true; }
    if (s.isEmpty() || s.compare("Substring", Qt::CaseInsensitive) == 0) {
        *out = MatchMode::Substring; return true;
    }
    return false;
}

Error TatSerializer::write(const QString& path,
                           const std::vector<FilterRule>& rules) {
    const QString tmp = path + ".tmp";
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {ErrCode::IoError, "TatSerializer::write",
                "cannot open tmp file: " + f.errorString().toStdString()};

    QXmlStreamWriter w(&f);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(2);
    w.writeStartDocument("1.0");
    w.writeStartElement("filters");
    for (const auto& r : rules) {
        w.writeStartElement("filter");
        w.writeAttribute("id", QString::number(r.id));
        w.writeAttribute("foreColor", colorToHex(r.foreground));
        w.writeAttribute("backColor", colorToHex(r.background));
        w.writeAttribute("pattern", QString::fromStdString(r.pattern));
        w.writeAttribute("isInclude", actToStr(r.action));
        w.writeAttribute("matchMode", modeToStr(r.mode));
        w.writeAttribute("caseSensitive", r.caseSensitive ? "1" : "0");
        w.writeAttribute("isEnabled", r.isEnabled ? "1" : "0");
        w.writeAttribute("word", r.wholeWord ? "1" : "0");       // 扩展可选属性
        w.writeAttribute("rank", QString::number(r.rank));       // 扩展可选属性
        w.writeAttribute("match", matchToStr(r.matchType));      // 扩展可选属性
        if (!r.description.empty())                              // 扩展可选属性
            w.writeAttribute("desc",
                             QString::fromStdString(r.description));
        w.writeEndElement();
    }
    w.writeEndElement();
    w.writeEndDocument();
    f.flush();
    if (f.error() != QFileDevice::NoError)
        return {ErrCode::IoError, "TatSerializer::write", "flush failed"};
    f.close();

    // 原子替换 + 备份（ARCH-UB §7.2 MUST）
    if (QFile::exists(path)) {
        const QString bak = path + ".bak";
        QFile::remove(bak);
        QFile::rename(path, bak);
    }
    if (!QFile::rename(tmp, path))
        return {ErrCode::IoError, "TatSerializer::write", "rename tmp failed"};
    return Error::none();
}

Error TatSerializer::read(const QString& path, std::vector<FilterRule>* out) {
    if (!out) return {ErrCode::InvalidArgument, "TatSerializer::read", "null out"};
    out->clear();

    QFile f(path);
    if (!f.exists()) return Error::none();  // 无文件 → 无规则（合法）
    if (!f.open(QIODevice::ReadOnly))
        return {ErrCode::IoError, "TatSerializer::read",
                "cannot open: " + f.errorString().toStdString()};

    QXmlStreamReader x(&f);
    while (!x.atEnd() && !x.hasError()) {
        x.readNext();
        if (x.isStartElement() && x.name() == QLatin1String("filter")) {
            const auto attrs = x.attributes();
            FilterRule r;
            bool ok = true;
            ok = ok && strToAct(attrs.value("isInclude").toString(), &r.action);
            ok = ok && strToMode(attrs.value("matchMode").toString(), &r.mode);
            ok = ok && hexToColor(attrs.value("foreColor").toString(), &r.foreground);
            ok = ok && hexToColor(attrs.value("backColor").toString(), &r.background);
            r.id = attrs.value("id").toInt(&ok);
            r.pattern = attrs.value("pattern").toString().toStdString();
            r.caseSensitive = attrs.value("caseSensitive").toString() == "1";
            r.isEnabled = attrs.value("isEnabled").toString() != "0";
            r.wholeWord = attrs.value("word").toString() == "1";
            r.rank = attrs.value("rank").toInt();
            ok = ok && strToMatch(attrs.value("match").toString(),
                                  &r.matchType);
            r.description =
                attrs.value("desc").toString().toStdString();
            if (!ok) {
                return {ErrCode::XmlError, "TatSerializer::read",
                        "bad attribute in filter element"};
            }
            out->push_back(r);
        }
    }
    if (x.hasError())
        return {ErrCode::XmlError, "TatSerializer::read",
                "XML error: " + x.errorString().toStdString()};
    return Error::none();
}

}  // namespace tat