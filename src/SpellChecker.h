#pragma once
#include <QByteArray>
#include <QSet>
#include <QString>
#include <QStringList>

// Built-in English spell checker: a bundled SCOWL word list (see resources/dict) plus a personal
// dictionary, with edit-distance suggestions. No external libraries or services are involved.
//
// Memory: the word list is a sorted, newline separated, *uncompressed* Qt resource that is binary
// searched in place (it stays file-backed in the executable image, never copied to the heap), instead
// of ~160k individually allocated QStrings in a hash set (~24 MB).
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
    bool inDictionary(const QString& lower, bool* noSuggest = nullptr) const;
    QSet<QString> edits1(const QString& w) const;

    bool m_loaded = false;
    const char* m_data = nullptr;      // sorted word list ("word\n" or "word!\n")
    qsizetype m_size = 0;
    QByteArray m_owned;                // only used when the resource turns out to be compressed
    qsizetype m_bucket[257] = {};      // start offset of the words beginning with byte b (+ end sentinel)
    QSet<QString> m_user;              // personal dictionary
    QSet<QString> m_session;           // "ignore" for this run
};
