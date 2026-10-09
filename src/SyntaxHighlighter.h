#pragma once
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

// Lightweight built-in highlighter (C/C++, Python, CMake, JSON, generic).
// Used as a fallback when KSyntaxHighlighting is not available or has no
// definition for the file type.
class SyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    SyntaxHighlighter(QTextDocument* doc, const QString& filePath);

protected:
    void highlightBlock(const QString& text) override;

private:
    enum Kind { Keyword, Type, String, Number, Comment, Preproc, Function, KindCount };
    struct Rule { QRegularExpression re; Kind kind; };
    struct Lang {
        QStringList keywords, types;
        QString lineComment, blockStart, blockEnd;
        QString stringDelims = QStringLiteral("\"");
        bool preproc = false;
        bool caseInsensitive = false;
    };

    void setupLanguage(const QString& filePath);
    void buildFormats();
    static QString wordsRegex(const QStringList& w);

    QVector<Rule> m_rules;
    QTextCharFormat m_fmt[KindCount];
    Lang m_lang;
};
