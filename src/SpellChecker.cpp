#include "SpellChecker.h"

#include <QFile>
#include <QSettings>

#include <algorithm>

SpellChecker& SpellChecker::instance()
{
    static SpellChecker sc;
    return sc;
}

void SpellChecker::ensureLoaded()
{
    if (m_loaded) return;
    m_loaded = true;

    QFile f(QStringLiteral(":/dict/en_US.txt"));
    if (f.open(QIODevice::ReadOnly)) {
        const QByteArray data = f.readAll();
        m_words.reserve(170000);
        for (const QByteArray& line : data.split('\n')) {
            if (line.isEmpty()) continue;
            if (line.startsWith('!')) {
                const QString w = QString::fromUtf8(line.mid(1));
                m_words.insert(w);
                m_noSuggest.insert(w);
            } else {
                m_words.insert(QString::fromUtf8(line));
            }
        }
    }
    const QStringList user = QSettings().value(QStringLiteral("spell/userWords")).toStringList();
    for (const QString& w : user) m_user.insert(w.toLower());
}

bool SpellChecker::known(const QString& lower) const
{
    return m_words.contains(lower) || m_user.contains(lower) || m_session.contains(lower);
}

bool SpellChecker::isCorrect(const QString& wordIn)
{
    ensureLoaded();
    if (m_words.isEmpty()) return true;            // dictionary missing: never nag

    QString w = wordIn;
    w.replace(QChar(0x2019), QLatin1Char('\''));
    while (w.startsWith(QLatin1Char('\''))) w.remove(0, 1);
    while (w.endsWith(QLatin1Char('\'')) || w.endsWith(QLatin1Char('.'))) w.chop(1);
    if (w.size() <= 1) return true;

    const QString lower = w.toLower();
    if (known(lower)) return true;
    if (lower.endsWith(QLatin1String("'s")) && known(lower.chopped(2))) return true;
    if (lower.endsWith(QLatin1String("s'")) && known(lower.chopped(1))) return true;
    if (lower.contains(QLatin1Char('-'))) {        // hyphenated compounds: every part must be a word
        const QStringList parts = lower.split(QLatin1Char('-'), Qt::SkipEmptyParts);
        if (parts.isEmpty()) return true;
        for (const QString& p : parts)
            if (p.size() > 1 && !known(p)) return false;
        return true;
    }
    return false;
}

void SpellChecker::addToDictionary(const QString& word)
{
    ensureLoaded();
    const QString lower = word.toLower();
    if (lower.isEmpty()) return;
    m_user.insert(lower);
    QSettings s;
    QStringList list = s.value(QStringLiteral("spell/userWords")).toStringList();
    if (!list.contains(lower)) list << lower;
    s.setValue(QStringLiteral("spell/userWords"), list);
}

void SpellChecker::ignoreForSession(const QString& word)
{
    ensureLoaded();
    m_session.insert(word.toLower());
}

QSet<QString> SpellChecker::edits1(const QString& w) const
{
    static const QString letters = QStringLiteral("abcdefghijklmnopqrstuvwxyz'");
    QSet<QString> out;
    const int n = int(w.size());
    out.reserve(n * 54 + 30);
    for (int i = 0; i < n; ++i) {
        out.insert(w.left(i) + w.mid(i + 1));                                           // delete
        if (i + 1 < n) out.insert(w.left(i) + w.at(i + 1) + w.at(i) + w.mid(i + 2));   // transpose
        for (const QChar c : letters) out.insert(w.left(i) + c + w.mid(i + 1));        // replace
    }
    for (int i = 0; i <= n; ++i)
        for (const QChar c : letters) out.insert(w.left(i) + c + w.mid(i));            // insert
    return out;
}

QStringList SpellChecker::suggest(const QString& wordIn, int maxSuggestions)
{
    ensureLoaded();
    if (m_words.isEmpty()) return {};

    QString word = wordIn;
    word.replace(QChar(0x2019), QLatin1Char('\''));
    const QString lower = word.toLower();
    if (lower.isEmpty() || lower.size() > 24) return {};

    struct Cand { QString w; int dist; };
    QList<Cand> cands;
    QSet<QString> seen;

    const QSet<QString> e1 = edits1(lower);
    for (const QString& c : e1)
        if (m_words.contains(c) && !m_noSuggest.contains(c) && !seen.contains(c)) { seen.insert(c); cands.append(Cand{c, 1}); }

    if (cands.size() < 3 && lower.size() <= 12) {            // two edits away, only when needed
        for (const QString& a : e1) {
            for (const QString& c : edits1(a))
                if (m_words.contains(c) && !m_noSuggest.contains(c) && !seen.contains(c)) { seen.insert(c); cands.append(Cand{c, 2}); }
            if (cands.size() > 200) break;
        }
    }
    for (const QString& u : std::as_const(m_user))           // personal words that are close
        if (e1.contains(u) && !seen.contains(u)) { seen.insert(u); cands.append(Cand{u, 1}); }

    const QChar first = lower.at(0);
    std::stable_sort(cands.begin(), cands.end(), [&](const Cand& a, const Cand& b) {
        if (a.dist != b.dist) return a.dist < b.dist;
        const bool fa = a.w.at(0) == first, fb = b.w.at(0) == first;
        if (fa != fb) return fa;
        const int da = qAbs(int(a.w.size()) - int(lower.size())), db = qAbs(int(b.w.size()) - int(lower.size()));
        if (da != db) return da < db;
        return a.w < b.w;
    });

    const bool allCaps = word.size() > 1 && word == word.toUpper();
    const bool capital = word.at(0).isUpper();
    QStringList result;
    for (const Cand& c : std::as_const(cands)) {
        QString s = c.w;
        if (allCaps) s = s.toUpper();
        else if (capital) s[0] = s[0].toUpper();
        result << s;
        if (result.size() >= maxSuggestions) break;
    }
    return result;
}
