#pragma once
#include <QList>
#include <QString>
#include <QStringList>

struct Snippet {
    QString trigger;          // typed word + Tab expands it
    QString description;
    QString body;             // VS Code style: $1, ${1:default}, ${1|a,b|}, $0, $TM_FILENAME ...
    QStringList languages;    // file extensions or aliases ("c++", "html", "*"); empty = every file
    bool builtin = false;
};

struct SnippetContext {
    QString fileName;         // base name with extension
    QString selectedText;
    QString clipboard;
};

struct ExpandedSnippet {
    struct Stop { int index = 0; int start = 0; int end = 0; };
    QString text;
    QList<Stop> stops;        // offsets into `text`; index 0 is the final cursor position
};

// Snippet store (built-in + user snippets in AppData/snippets.json) and body parser.
class SnippetManager {
public:
    static SnippetManager& instance();

    QList<Snippet> builtinSnippets() const;
    QList<Snippet> userSnippets() const;
    void setUserSnippets(const QList<Snippet>& list);       // persists

    // Snippets applicable to a file; user snippets come first and override built-ins with the same trigger.
    QList<Snippet> forFile(const QString& path) const;
    bool find(const QString& path, const QString& trigger, Snippet* out) const;

    bool exportTo(const QList<Snippet>& list, const QString& path, QString* error = nullptr) const;
    bool importFrom(const QString& path, QList<Snippet>* out, QString* error = nullptr) const;

    static bool appliesTo(const Snippet& s, const QString& path);
    // `indent` is added to every line after the first; tabs in the body become `tabSize` spaces.
    static ExpandedSnippet expand(const QString& body, const QString& indent, int tabSize, const SnippetContext& ctx);

private:
    SnippetManager() = default;
    mutable QList<Snippet> m_user;
    mutable bool m_loaded = false;
    void load() const;
};
