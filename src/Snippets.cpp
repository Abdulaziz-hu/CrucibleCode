#include "Snippets.h"

#include <QDate>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDir>

SnippetManager& SnippetManager::instance()
{
    static SnippetManager m;
    return m;
}

static QString storePath()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(d);
    return d + QStringLiteral("/snippets.json");
}

static Snippet S(const char* trig, const char* desc, const char* langs, const char* body)
{
    Snippet s;
    s.trigger = QString::fromLatin1(trig);
    s.description = QString::fromUtf8(desc);
    s.languages = QString::fromLatin1(langs).split(QLatin1Char(','), Qt::SkipEmptyParts);
    s.body = QString::fromUtf8(body);
    s.builtin = true;
    return s;
}

QList<Snippet> SnippetManager::builtinSnippets() const
{
    static const QList<Snippet> list = {
        S("main", "int main()", "c++,c", "int main(int argc, char* argv[])\n{\n\t$0\n\treturn 0;\n}"),
        S("for", "for loop", "c++,c,cs,js,ts,java", "for (int ${1:i} = 0; $1 < ${2:count}; ++$1) {\n\t$0\n}"),
        S("forr", "range-based for", "c++", "for (const auto& ${1:item} : ${2:container}) {\n\t$0\n}"),
        S("while", "while loop", "c++,c,cs,js,ts,java", "while (${1:condition}) {\n\t$0\n}"),
        S("if", "if statement", "c++,c,cs,js,ts,java", "if (${1:condition}) {\n\t$0\n}"),
        S("ife", "if / else", "c++,c,cs,js,ts,java", "if (${1:condition}) {\n\t$2\n} else {\n\t$0\n}"),
        S("switch", "switch statement", "c++,c,cs,js,ts,java", "switch (${1:value}) {\ncase ${2:x}:\n\t$0\n\tbreak;\ndefault:\n\tbreak;\n}"),
        S("try", "try / catch", "c++,cs,java,js,ts", "try {\n\t$1\n} catch (const ${2:std::exception}& ${3:e}) {\n\t$0\n}"),
        S("class", "C++ class", "c++", "class ${1:$TM_FILENAME_BASE} {\npublic:\n\t$1();\n\t~$1();\n\nprivate:\n\t$0\n};"),
        S("struct", "struct", "c++,c", "struct ${1:Name} {\n\t$0\n};"),
        S("ns", "namespace", "c++", "namespace ${1:name} {\n\n$0\n\n} // namespace $1"),
        S("inc", "#include <...>", "c++,c", "#include <${1:iostream}>"),
        S("incl", "#include \"...\"", "c++,c", "#include \"${1:header}.h\""),
        S("once", "#pragma once", "c++,c", "#pragma once\n"),
        S("guard", "include guard", "c++,c", "#ifndef ${1:HEADER_H}\n#define $1\n\n$0\n\n#endif // $1"),
        S("lambda", "lambda", "c++", "[${1:&}](${2:}) {\n\t$0\n}"),
        S("uptr", "std::unique_ptr", "c++", "std::unique_ptr<${1:T}> ${2:p} = std::make_unique<$1>($0);"),
        S("sptr", "std::shared_ptr", "c++", "std::shared_ptr<${1:T}> ${2:p} = std::make_shared<$1>($0);"),
        S("cout", "std::cout", "c++", "std::cout << ${1:value} << '\\n';"),
        S("qobj", "Qt QObject class", "c++", "class ${1:MyClass} : public QObject {\n\tQ_OBJECT\npublic:\n\texplicit $1(QObject* parent = nullptr);\n\nsignals:\n\t$0\n};"),
        S("qconn", "Qt connect()", "c++", "connect(${1:sender}, &${2:Class}::${3:signal}, ${4:this}, &${5:Class}::${6:slot});"),
        S("qdebug", "qDebug()", "c++", "qDebug() << ${1:value};"),
        S("qstr", "QStringLiteral", "c++", "QStringLiteral(\"${1:text}\")"),
        S("qlay", "Qt layout", "c++", "auto* ${1:layout} = new QVBoxLayout(${2:this});\n$1->addWidget(${3:widget});$0"),
        S("cw", "Console.WriteLine", "cs", "Console.WriteLine(${1:value});"),
        S("prop", "C# property", "cs", "public ${1:int} ${2:Name} { get; set; }"),
        S("def", "Python function", "py", "def ${1:name}(${2:args}):\n\t${0:pass}"),
        S("ifmain", "Python main guard", "py", "if __name__ == \"__main__\":\n\t${0:main()}"),
        S("html5", "HTML5 page", "html,htm", "<!DOCTYPE html>\n<html lang=\"${1:en}\">\n<head>\n\t<meta charset=\"utf-8\">\n\t<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n\t<title>${2:Title}</title>\n</head>\n<body>\n\t$0\n</body>\n</html>"),
        S("div", "div", "html,htm", "<div class=\"${1:class}\">\n\t$0\n</div>"),
        S("lorem", "placeholder text", "html,htm,md,txt", "Lorem ipsum dolor sit amet, consectetur adipiscing elit."),
        S("todo", "TODO comment", "", "TODO($TM_FILENAME_BASE): $0"),
        S("date", "current date", "", "$CURRENT_DATE"),
        S("lic", "MIT copyright line", "", "Copyright (c) $CURRENT_YEAR ${1:Your Name}"),
    };
    return list;
}

void SnippetManager::load() const
{
    if (m_loaded) return;
    m_loaded = true;
    m_user.clear();
    QList<Snippet> l;
    QString err;
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray a = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("snippets")).toArray();
    for (const QJsonValue& v : a) {
        const QJsonObject o = v.toObject();
        Snippet s;
        s.trigger = o.value(QStringLiteral("trigger")).toString();
        if (s.trigger.isEmpty()) continue;
        s.description = o.value(QStringLiteral("description")).toString();
        s.body = o.value(QStringLiteral("body")).toString();
        for (const QJsonValue& lv : o.value(QStringLiteral("languages")).toArray()) s.languages << lv.toString();
        m_user << s;
    }
}

QList<Snippet> SnippetManager::userSnippets() const { load(); return m_user; }

static QJsonObject toJson(const Snippet& s)
{
    return {{QStringLiteral("trigger"), s.trigger}, {QStringLiteral("description"), s.description},
            {QStringLiteral("body"), s.body}, {QStringLiteral("languages"), QJsonArray::fromStringList(s.languages)}};
}

bool SnippetManager::exportTo(const QList<Snippet>& list, const QString& path, QString* error) const
{
    QJsonArray a;
    for (const Snippet& s : list) a.append(toJson(s));
    const QJsonObject root{{QStringLiteral("format"), QStringLiteral("cruciblecode-snippets")}, {QStringLiteral("version"), 1},
                           {QStringLiteral("snippets"), a}};
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (error) *error = f.errorString(); return false; }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) { if (error) *error = f.errorString(); return false; }
    return true;
}

bool SnippetManager::importFrom(const QString& path, QList<Snippet>* out, QString* error) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = f.errorString(); return false; }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("cruciblecode-snippets")) {
        if (error) *error = QObject::tr("This is not a CrucibleCode snippets file.");
        return false;
    }
    for (const QJsonValue& v : root.value(QStringLiteral("snippets")).toArray()) {
        const QJsonObject o = v.toObject();
        Snippet s;
        s.trigger = o.value(QStringLiteral("trigger")).toString();
        if (s.trigger.isEmpty()) continue;
        s.description = o.value(QStringLiteral("description")).toString();
        s.body = o.value(QStringLiteral("body")).toString();
        for (const QJsonValue& lv : o.value(QStringLiteral("languages")).toArray()) s.languages << lv.toString();
        out->append(s);
    }
    return true;
}

void SnippetManager::setUserSnippets(const QList<Snippet>& list)
{
    load();
    m_user = list;
    for (Snippet& s : m_user) s.builtin = false;
    QString err;
    exportTo(m_user, storePath(), &err);
}

bool SnippetManager::appliesTo(const Snippet& s, const QString& path)
{
    if (s.languages.isEmpty()) return true;
    const QFileInfo fi(path);
    const QString ext = fi.suffix().toLower();
    static const QStringList cpp = {"c", "cc", "cpp", "cxx", "h", "hh", "hpp", "hxx", "inl", "ipp", "mm", "cu"};
    for (QString l : s.languages) {
        l = l.trimmed().toLower();
        if (l == QLatin1String("*") || l.isEmpty()) return true;
        if (l == ext) return true;
        if ((l == QLatin1String("c++") || l == QLatin1String("cpp")) && cpp.contains(ext) && ext != QLatin1String("c")) return true;
        if (l == QLatin1String("c") && (ext == QLatin1String("c") || ext == QLatin1String("h"))) return true;
        if ((l == QLatin1String("csharp") || l == QLatin1String("c#")) && ext == QLatin1String("cs")) return true;
        if (l == QLatin1String("python") && (ext == QLatin1String("py") || ext == QLatin1String("pyw"))) return true;
        if (l == QLatin1String("html") && (ext == QLatin1String("html") || ext == QLatin1String("htm"))) return true;
        if (l == QLatin1String("md") && (ext == QLatin1String("md") || ext == QLatin1String("markdown"))) return true;
    }
    return false;
}

QList<Snippet> SnippetManager::forFile(const QString& path) const
{
    load();
    QList<Snippet> out;
    QStringList seen;
    for (const Snippet& s : m_user)
        if (appliesTo(s, path)) { out << s; seen << s.trigger; }
    for (const Snippet& s : builtinSnippets())
        if (!seen.contains(s.trigger) && appliesTo(s, path)) out << s;
    return out;
}

bool SnippetManager::find(const QString& path, const QString& trigger, Snippet* out) const
{
    for (const Snippet& s : forFile(path))
        if (s.trigger == trigger) { if (out) *out = s; return true; }
    return false;
}

// ---------------------------------------------------------------------------------------------
//  Body parser
// ---------------------------------------------------------------------------------------------

ExpandedSnippet SnippetManager::expand(const QString& body, const QString& indent, int tabSize, const SnippetContext& ctx)
{
    ExpandedSnippet r;
    const QString tab(qMax(1, tabSize), QLatin1Char(' '));

    const auto put = [&](const QString& s) {
        for (QChar ch : s) {
            if (ch == QLatin1Char('\n')) r.text += QLatin1Char('\n') + indent;
            else if (ch == QLatin1Char('\t')) r.text += tab;
            else r.text += ch;
        }
    };
    const auto variable = [&](const QString& name, bool* known) -> QString {
        *known = true;
        const QFileInfo fi(ctx.fileName);
        if (name == QLatin1String("TM_FILENAME")) return ctx.fileName.isEmpty() ? QStringLiteral("Untitled") : ctx.fileName;
        if (name == QLatin1String("TM_FILENAME_BASE")) return fi.completeBaseName().isEmpty() ? QStringLiteral("Untitled") : fi.completeBaseName();
        if (name == QLatin1String("TM_SELECTED_TEXT")) return ctx.selectedText;
        if (name == QLatin1String("CLIPBOARD")) return ctx.clipboard;
        if (name == QLatin1String("CURRENT_YEAR")) return QString::number(QDate::currentDate().year());
        if (name == QLatin1String("CURRENT_MONTH")) return QDate::currentDate().toString(QStringLiteral("MM"));
        if (name == QLatin1String("CURRENT_DATE")) return QDate::currentDate().toString(Qt::ISODate);
        *known = false;
        return {};
    };

    const int n = int(body.size());
    for (int i = 0; i < n;) {
        const QChar c = body.at(i);
        if (c == QLatin1Char('\\') && i + 1 < n && (body.at(i + 1) == QLatin1Char('$') || body.at(i + 1) == QLatin1Char('\\') || body.at(i + 1) == QLatin1Char('}'))) {
            put(QString(body.at(i + 1)));
            i += 2;
            continue;
        }
        if (c != QLatin1Char('$') || i + 1 >= n) { put(QString(c)); ++i; continue; }

        // $N
        if (body.at(i + 1).isDigit()) {
            int j = i + 1;
            while (j < n && body.at(j).isDigit()) ++j;
            const int index = body.mid(i + 1, j - i - 1).toInt();
            const int start = int(r.text.size());
            QString mirrored;                                  // a later $N repeats the text of the first one
            for (const auto& st : r.stops)
                if (st.index == index && index != 0) { mirrored = r.text.mid(st.start, st.end - st.start); break; }
            put(mirrored);
            r.stops.append({index, start, int(r.text.size())});
            i = j;
            continue;
        }
        // $NAME
        if (body.at(i + 1).isLetter() || body.at(i + 1) == QLatin1Char('_')) {
            int j = i + 1;
            while (j < n && (body.at(j).isLetterOrNumber() || body.at(j) == QLatin1Char('_'))) ++j;
            bool known = false;
            const QString v = variable(body.mid(i + 1, j - i - 1), &known);
            if (known) put(v); else put(body.mid(i, j - i));
            i = j;
            continue;
        }
        // ${...}
        if (body.at(i + 1) == QLatin1Char('{')) {
            int depth = 1, j = i + 2;
            while (j < n && depth > 0) {
                if (body.at(j) == QLatin1Char('\\')) { j += 2; continue; }
                if (body.at(j) == QLatin1Char('{')) ++depth;
                else if (body.at(j) == QLatin1Char('}')) --depth;
                ++j;
            }
            if (depth != 0) { put(QString(c)); ++i; continue; }
            const QString inner = body.mid(i + 2, j - i - 3);
            int k = 0;
            while (k < inner.size() && inner.at(k).isDigit()) ++k;
            if (k > 0) {                                  // ${1}, ${1:default}, ${1|a,b|}
                const int index = inner.left(k).toInt();
                QString def;
                if (k < inner.size() && inner.at(k) == QLatin1Char(':')) def = inner.mid(k + 1);
                else if (k < inner.size() && inner.at(k) == QLatin1Char('|')) def = inner.mid(k + 1).section(QLatin1Char(','), 0, 0).remove(QLatin1Char('|'));
                // defaults may reference variables, e.g. ${1:$TM_FILENAME_BASE}
                if (def.startsWith(QLatin1Char('$')) && def.size() > 1 && def.at(1).isLetter()) {
                    bool known = false;
                    const QString v = variable(def.mid(1), &known);
                    if (known) def = v;
                }
                const int start = int(r.text.size());
                // a mirror of an earlier stop with the same index repeats its text
                QString text = def;
                for (const auto& s : r.stops)
                    if (s.index == index && text.isEmpty()) text = r.text.mid(s.start, s.end - s.start);
                put(text);
                r.stops.append({index, start, int(r.text.size())});
            } else {                                      // ${NAME} or ${NAME:default}
                const QString name = inner.section(QLatin1Char(':'), 0, 0);
                bool known = false;
                QString v = variable(name, &known);
                if (known && v.isEmpty() && inner.contains(QLatin1Char(':'))) v = inner.section(QLatin1Char(':'), 1);
                put(known ? v : inner.section(QLatin1Char(':'), 1));
            }
            i = j;
            continue;
        }
        put(QString(c));
        ++i;
    }
    return r;
}
