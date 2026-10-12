#pragma once
#include <QSet>
#include <QString>
#include <QStringList>

// Built-in English spell checker: a bundled SCOWL word list (see resources/dict) plus a personal
// dictionary, with edit-distance suggestions. No external libraries or services are involved.
class SpellChecker {
public:
    static SpellChecker& instance();

    bool isCorrect(const QString& word);
    QStringList suggest(const QString& word, int maxSuggestions = 6);

    void addToDictionary(const QString& word);   // saved in the settings
    void ignoreForSession(const QString& word);

private:
    SpellChecker() = default;
    void ensureLoaded();
    bool known(const QString& lower) const;
    QSet<QString> edits1(const QString& w) const;

    bool m_loaded = false;
    QSet<QString> m_words;       // accepted words (lower case)
    QSet<QString> m_noSuggest;   // accepted, but never offered as a correction
    QSet<QString> m_user;        // personal dictionary
    QSet<QString> m_session;     // "ignore" for this run
};
