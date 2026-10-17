#include "ThemeCss.h"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>

namespace ThemeCss {

static QString T(const char* text) { return QCoreApplication::translate("ThemeCss", text); }

// "line-highlight", "lineHighlight" and "line_highlight" are all the same key
static QString normalizeKey(QString s)
{
    s.remove(QLatin1Char('-'));
    s.remove(QLatin1Char('_'));
    return s.toLower();
}

QString keyToVariable(const QString& key)
{
    QString out;
    for (const QChar ch : key) {
        if (ch.isUpper()) {
            out += QLatin1Char('-');
            out += ch.toLower();
        } else {
            out += ch;
        }
    }
    return out;
}

// ---- colours -------------------------------------------------------------------------------

static bool channel(const QString& s, double scale, int* out)
{
    bool ok = false;
    if (s.endsWith(QLatin1Char('%'))) {
        const double v = s.chopped(1).toDouble(&ok);
        if (!ok) return false;
        *out = qBound(0, qRound(v / 100.0 * scale), int(scale));
    } else {
        const double v = s.toDouble(&ok);
        if (!ok) return false;
        *out = qBound(0, qRound(scale == 255 ? v : v * scale), int(scale));
    }
    return true;
}

QColor parseColor(const QString& textIn, bool* ok)
{
    if (ok) *ok = false;
    QString s = textIn.trimmed();
    if (s.endsWith(QLatin1String("!important"), Qt::CaseInsensitive)) s = s.chopped(10).trimmed();
    if (s.isEmpty()) return {};

    if (s.startsWith(QLatin1Char('#'))) {
        const QString h = s.mid(1);
        static const QRegularExpression hexRe(QStringLiteral("^[0-9a-fA-F]+$"));
        if (!hexRe.match(h).hasMatch()) return {};
        const auto pair = [&h](int i, bool doubled) {
            return (doubled ? h.mid(i, 1).repeated(2) : h.mid(i, 2)).toInt(nullptr, 16);
        };
        QColor c;
        switch (h.size()) {
        case 3: c = QColor(pair(0, true), pair(1, true), pair(2, true)); break;
        case 4: c = QColor(pair(0, true), pair(1, true), pair(2, true), pair(3, true)); break;
        case 6: c = QColor(pair(0, false), pair(2, false), pair(4, false)); break;
        case 8: c = QColor(pair(0, false), pair(2, false), pair(4, false), pair(6, false)); break;
        default: return {};
        }
        if (ok) *ok = true;
        return c;
    }

    static const QRegularExpression fnRe(
        QStringLiteral(R"(^rgba?\(\s*([0-9.]+%?)\s*[,\s]\s*([0-9.]+%?)\s*[,\s]\s*([0-9.]+%?)\s*(?:[,/]\s*([0-9.]+%?)\s*)?\)$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = fnRe.match(s);
    if (m.hasMatch()) {
        int r, g, b, a = 255;
        if (!channel(m.captured(1), 255, &r) || !channel(m.captured(2), 255, &g) || !channel(m.captured(3), 255, &b)) return {};
        if (!m.captured(4).isEmpty() && !channel(m.captured(4), 255, &a)) return {};
        // alpha is 0..1 (or a percentage) in CSS
        if (!m.captured(4).isEmpty() && !m.captured(4).endsWith(QLatin1Char('%'))) {
            bool k = false;
            const double av = m.captured(4).toDouble(&k);
            if (!k) return {};
            a = qBound(0, qRound(av * 255.0), 255);
        }
        if (ok) *ok = true;
        return QColor(r, g, b, a);
    }

    static const QRegularExpression nameRe(QStringLiteral("^[A-Za-z]+$"));
    if (nameRe.match(s).hasMatch()) {
        const QColor c(s);
        if (c.isValid()) {
            if (ok) *ok = true;
            return c;
        }
    }
    return {};
}

QString colorToCss(const QColor& c)
{
    if (c.alpha() == 255) return c.name(QColor::HexRgb);
    return QStringLiteral("#%1%2%3%4")
        .arg(c.red(), 2, 16, QLatin1Char('0'))
        .arg(c.green(), 2, 16, QLatin1Char('0'))
        .arg(c.blue(), 2, 16, QLatin1Char('0'))
        .arg(c.alpha(), 2, 16, QLatin1Char('0'));
}

// ---- parsing -------------------------------------------------------------------------------

static bool leadingNumber(const QString& s, double* out)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*([0-9]*\.?[0-9]+))"));
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch()) return false;
    *out = m.captured(1).toDouble();
    return true;
}

Result parse(const QString& css)
{
    Result r;
    if (css.size() > kMaxFileBytes) {
        r.error = T("The theme file is too large.");
        return r;
    }

    // 1. metadata: "@key value" lines inside comments
    static const QRegularExpression commentRe(QStringLiteral(R"(/\*.*?\*/)"), QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression metaRe(QStringLiteral(R"(^[ \t*/]*@([A-Za-z-]+)[ \t]+(.*?)[ \t]*(?:\*/)?[ \t]*$)"),
                                           QRegularExpression::MultilineOption);
    QHash<QString, QString> meta;
    QRegularExpressionMatchIterator ci = commentRe.globalMatch(css);
    while (ci.hasNext()) {
        const QString block = ci.next().captured(0);
        QRegularExpressionMatchIterator mi = metaRe.globalMatch(block);
        while (mi.hasNext()) {
            const QRegularExpressionMatch m = mi.next();
            const QString k = m.captured(1).toLower();
            if (!meta.contains(k)) meta.insert(k, m.captured(2).trimmed());
        }
    }

    // 2. variables: every "--name: value" declaration (normally inside :root { })
    QString body = css;
    body.remove(commentRe);
    static const QRegularExpression rootRe(QStringLiteral(R"(:root\s*\{([^}]*)\})"));
    QString decls;
    QRegularExpressionMatchIterator ri = rootRe.globalMatch(body);
    while (ri.hasNext()) decls += ri.next().captured(1) + QLatin1Char(';');
    if (decls.isEmpty()) decls = body;     // tolerate a bare list of variables

    static const QRegularExpression declRe(QStringLiteral(R"((?:^|[\s{])--([A-Za-z0-9_-]+)\s*:\s*(.+?)\s*$)"),
                                           QRegularExpression::DotMatchesEverythingOption);
    QHash<QString, QString> vars;           // normalised name -> value
    QHash<QString, QString> originalName;   // normalised name -> name as written
    for (const QString& chunk : decls.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch m = declRe.match(chunk);
        if (!m.hasMatch()) continue;
        const QString n = normalizeKey(m.captured(1));
        vars.insert(n, m.captured(2).trimmed());
        originalName.insert(n, m.captured(1));
    }
    if (vars.isEmpty()) {
        r.error = T("No theme variables found. A theme needs a \":root { --window: #...; }\" block.");
        return r;
    }

    // 3. dark or light? decides which built-in theme fills in anything the file leaves out
    QString type = meta.value(QStringLiteral("type")).toLower();
    if (type != QLatin1String("dark") && type != QLatin1String("light")) type.clear();
    if (type.isEmpty()) {
        bool ok = false;
        const QColor b = parseColor(vars.value(QStringLiteral("base")), &ok);
        type = (ok && b.lightness() >= 128) ? QStringLiteral("light") : QStringLiteral("dark");
    }
    ThemeColors colors = ThemeManager::baseColors(type == QLatin1String("light") ? ThemeManager::Light : ThemeManager::DarkGrey);

    QStringList known = {QStringLiteral("radius"), QStringLiteral("uifontsize")};
    for (const ThemeColors::Field& f : ThemeColors::fields()) {
        const QString key = QString::fromLatin1(f.key);
        known << normalizeKey(key);
        const auto vi = vars.constFind(normalizeKey(key));
        if (vi == vars.constEnd()) {
            r.missing << key;
            continue;
        }
        bool ok = false;
        const QColor c = parseColor(vi.value(), &ok);
        if (!ok) {
            r.error = T("Invalid colour for --%1: \"%2\"").arg(keyToVariable(key), vi.value());
            return r;
        }
        colors.*f.member = c;
    }
    for (const char* essential : {"window", "base", "text"}) {
        if (r.missing.contains(QLatin1String(essential))) {
            r.error = T("The theme must define at least --window, --base and --text.");
            return r;
        }
    }

    double v = 0;
    if (vars.contains(QStringLiteral("radius")) && leadingNumber(vars.value(QStringLiteral("radius")), &v))
        colors.radius = qBound(0, qRound(v), 10);
    if (vars.contains(QStringLiteral("uifontsize")) && leadingNumber(vars.value(QStringLiteral("uifontsize")), &v))
        colors.uiFontSize = (v >= 6 && v <= 24) ? v : 0;

    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
        if (!known.contains(it.key())) r.unknown << originalName.value(it.key());
    r.unknown.sort();

    r.colors = colors;
    r.meta.name = meta.value(QStringLiteral("name"));
    r.meta.author = meta.value(QStringLiteral("author"));
    r.meta.version = meta.value(QStringLiteral("version"));
    r.meta.description = meta.value(QStringLiteral("description"));
    r.meta.credits = meta.value(QStringLiteral("credits"));
    r.meta.type = type;
    r.ok = true;
    return r;
}

// ---- writing -------------------------------------------------------------------------------

static QString oneLine(QString s)
{
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    s.replace(QLatin1Char('\r'), QLatin1Char(' '));
    s.replace(QStringLiteral("*/"), QStringLiteral("* /"));     // must not end the header comment
    return s.trimmed();
}

QString serialize(const ThemeColors& c, const Meta& m)
{
    QString out;
    out += QStringLiteral("/*\n");
    out += QStringLiteral(" * @name        %1\n").arg(oneLine(m.name));
    out += QStringLiteral(" * @author      %1\n").arg(oneLine(m.author));
    out += QStringLiteral(" * @version     %1\n").arg(oneLine(m.version.isEmpty() ? QStringLiteral("1.0.0") : m.version));
    const QString type = (m.type == QLatin1String("light") || m.type == QLatin1String("dark"))
                             ? m.type : (c.window.lightness() < 128 ? QStringLiteral("dark") : QStringLiteral("light"));
    out += QStringLiteral(" * @type        %1\n").arg(type);
    out += QStringLiteral(" * @description %1\n").arg(oneLine(m.description));
    if (!m.credits.isEmpty()) out += QStringLiteral(" * @credits     %1\n").arg(oneLine(m.credits));
    out += QStringLiteral(" */\n\n:root {\n");

    QString group;
    for (const ThemeColors::Field& f : ThemeColors::fields()) {
        if (group != QLatin1String(f.group)) {
            if (!group.isEmpty()) out += QLatin1Char('\n');
            group = QString::fromLatin1(f.group);
            out += QStringLiteral("  /* %1 */\n").arg(group);
        }
        out += QStringLiteral("  --%1: %2;\n").arg(keyToVariable(QString::fromLatin1(f.key)), colorToCss(c.*f.member));
    }
    out += QStringLiteral("\n  /* Options */\n  --radius: %1px;\n").arg(c.radius);
    if (c.uiFontSize > 0) out += QStringLiteral("  --ui-font-size: %1pt;\n").arg(c.uiFontSize, 0, 'f', 1);
    out += QStringLiteral("}\n");
    return out;
}

}   // namespace ThemeCss
