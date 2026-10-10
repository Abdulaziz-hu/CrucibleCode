#pragma once
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

// Lightweight built-in highlighter: C, C++, C#, Markdown, CMake, Python, JSON.
// Used when KSyntaxHighlighting is not available or has no definition for the file.
class SyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    SyntaxHighlighter(QTextDocument* doc, const QString& filePath);

    // Languages with built-in support (shown in Help > About).
    static QStringList builtinLanguages();

protected:
    void highlightBlock(const QString& text) override;

private:
    enum Kind { Keyword, Type, String, Number, Comment, Preproc, Function,
                Heading, Bold, Italic, Code, Link, KindCount };
    struct Rule { QRegularExpression re; Kind kind; };
    struct Lang {
        QStringList keywords, types;
        QString lineComment, blockStart, blockEnd;
        QString stringDelims = QStringLiteral("\"");
        bool preproc = false;
        bool caseInsensitive = false;
        bool markdown = false;
    };

    void setupLanguage(const QString& filePath);
    void setupMarkdown();
    void buildFormats();
    void highlightMarkdown(const QString& text);
    static QString wordsRegex(const QStringList& w);

    QVector<Rule> m_rules;
    QTextCharFormat m_fmt[KindCount];
    Lang m_lang;
};
