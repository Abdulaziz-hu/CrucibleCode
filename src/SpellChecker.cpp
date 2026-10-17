#include "SpellChecker.h"

#include <QFile>
#include <QResource>
#include <QSettings>

#include <algorithm>
#include <cstring>

SpellChecker& SpellChecker::instance()
{
    static SpellChecker sc;
    return sc;
}

void SpellChecker::ensureLoaded()
{
    if (m_loaded) return;
    m_loaded = true;

    QResource res(QStringLiteral(":/dict/en_US.txt"));
    if (res.isValid()) {
        if (res.compressionAlgorithm() != QResource::NoCompression) {                    // fallback: built without -no-compress
            m_owned = res.uncompressedData();
            m_data = m_owned.constData();
            m_size = m_owned.size();
        } else {                                        // normal case: points into the executable image
            m_data = reinterpret_cast<const char*>(res.data());
            m_size = qsizetype(res.size());
        }
        // 257-entry first-byte index (~2 KB): m_bucket[b] = offset of the first line starting with a byte >= b.
        // Found by binary search, so only a handful of pages of the resource are ever touched.
        for (int byte = 0; byte <= 256; ++byte) {
            qsizetype lo = 0, hi = m_size;
            while (lo < hi) {
                qsizetype mid = lo + (hi - lo) / 2;
                qsizetype st = mid;
                while (st > lo && m_data[st - 1] != '\n') --st;
                if (int(uchar(m_data[st])) < byte) {
                    const char* nl = static_cast<const char*>(memchr(m_data + st, '\n', size_t(m_size - st)));
                    lo = nl ? qsizetype(nl - m_data) + 1 : m_size;
                } else {
                    hi = st;
                }
            }
            m_bucket[byte] = lo;
        }
    }
    const QStringList user = QSettings().value(QStringLiteral("spell/userWords")).toStringList();
    for (const QString& w : user) m_user.insert(w.toLower());
}

// Binary search over line start offsets inside [lo, hi). The stored line may end in '!' (not suggested).
bool SpellChecker::inDictionary(const QString& lower, bool* noSuggest) const
{
    if (!m_data || lower.isEmpty()) return false;
    const QByteArray key = lower.toUtf8();
    const uchar first = uchar(key.at(0));
    qsizetype lo = m_bucket[first], hi = m_bucket[first + 1];
    while (lo < hi) {
        qsizetype mid = lo + (hi - lo) / 2;
        qsizetype s = mid;
        while (s > lo && m_data[s - 1] != '\n') --s;                  // start of the line containing mid
        const char* nl = static_cast<const char*>(memchr(m_data + s, '\n', size_t(m_size - s)));
        const qsizetype e = nl ? qsizetype(nl - m_data) : m_size;      // end (exclusive) of that line
        qsizetype len = e - s;
        const bool flagged = len > 0 && m_data[e - 1] == '!';
        if (flagged) --len;
        const qsizetype n = qMin<qsizetype>(len, key.size());
        int c = n ? memcmp(m_data + s, key.constData(), size_t(n)) : 0;
        if (c == 0) c = len < key.size() ? -1 : (len > key.size() ? 1 : 0);
        if (c == 0) { if (noSuggest) *noSuggest = flagged; return true; }
        if (c < 0) lo = e + 1;
        else hi = s;
    }
    return false;
}

bool SpellChecker::known(const QString& lower) const
{
    return inDictionary(lower) || m_user.contains(lower) || m_session.contains(lower);
}

bool SpellChecker::isCorrect(const QString& wordIn)
{
    ensureLoaded();
    if (!m_data) return true;                      // dictionary missing: never nag

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
    if (!m_data) return {};

    QString word = wordIn;
    word.replace(QChar(0x2019), QLatin1Char('\''));
    const QString lower = word.toLower();
    if (lower.isEmpty() || lower.size() > 24) return {};

    struct Cand { QString w; int dist; };
    QList<Cand> cands;
    QSet<QString> seen;

    const QSet<QString> e1 = edits1(lower);
    const auto suggestable = [this](const QString& c) {
        bool ns = false;
        return inDictionary(c, &ns) && !ns;
    };
    for (const QString& c : e1)
        if (suggestable(c) && !seen.contains(c)) { seen.insert(c); cands.append(Cand{c, 1}); }

    if (cands.size() < 3 && lower.size() <= 12) {            // two edits away, only when needed
        for (const QString& a : e1) {
            for (const QString& c : edits1(a))
                if (suggestable(c) && !seen.contains(c)) { seen.insert(c); cands.append(Cand{c, 2}); }
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
