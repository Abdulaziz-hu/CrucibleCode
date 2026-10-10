#include "SyntaxHighlighter.h"
#include "ThemeManager.h"

#include <QFileInfo>

namespace {
const QStringList kCKeywords = {
    "auto","break","case","const","continue","default","do","else","enum","extern","for","goto",
    "if","inline","register","restrict","return","signed","sizeof","static","struct","switch",
    "typedef","union","unsigned","volatile","while","true","false","NULL","nullptr","constexpr",
    "typeof","_Alignas","_Alignof","_Atomic","_Generic","_Noreturn","_Static_assert","_Thread_local"};
const QStringList kCTypes = {
    "void","char","short","int","long","float","double","bool","_Bool","size_t","ssize_t","ptrdiff_t",
    "intptr_t","uintptr_t","int8_t","int16_t","int32_t","int64_t","uint8_t","uint16_t","uint32_t",
    "uint64_t","FILE","wchar_t"};

const QStringList kCppKeywords = {
    "alignas","alignof","and","asm","auto","break","case","catch","class","concept","const",
    "consteval","constexpr","constinit","const_cast","continue","co_await","co_return","co_yield",
    "decltype","default","delete","do","dynamic_cast","else","enum","explicit","export","extern",
    "false","final","for","friend","goto","if","inline","mutable","namespace","new","noexcept",
    "not","nullptr","operator","or","override","private","protected","public","register",
    "reinterpret_cast","requires","return","sizeof","static","static_assert","static_cast",
    "struct","switch","template","this","thread_local","throw","true","try","typedef","typeid",
    "typename","union","using","virtual","volatile","while","signals","slots","emit","foreach"};
const QStringList kCppTypes = {
    "void","bool","char","char8_t","char16_t","char32_t","wchar_t","short","int","long","float",
    "double","signed","unsigned","size_t","ssize_t","ptrdiff_t","intptr_t","uintptr_t","int8_t",
    "int16_t","int32_t","int64_t","uint8_t","uint16_t","uint32_t","uint64_t","string","vector",
    "map","set","unique_ptr","shared_ptr","QString","QObject","QWidget","qint64","quint64"};

const QStringList kCsKeywords = {
    "abstract","as","base","break","case","catch","checked","class","const","continue","default",
    "delegate","do","else","enum","event","explicit","extern","false","finally","fixed","for",
    "foreach","goto","if","implicit","in","interface","internal","is","lock","namespace","new",
    "null","operator","out","override","params","private","protected","public","readonly","ref",
    "return","sealed","sizeof","stackalloc","static","struct","switch","this","throw","true","try",
    "typeof","unchecked","unsafe","using","virtual","volatile","while","var","async","await",
    "yield","get","set","init","record","partial","where","select","from","dynamic","nameof","when",
    "required","file","global","with","and","or","not"};
const QStringList kCsTypes = {
    "bool","byte","char","decimal","double","float","int","long","object","sbyte","short","string",
    "uint","ulong","ushort","void","nint","nuint","String","Int32","Int64","List","Dictionary",
    "HashSet","Task","Console","Math","Object","Exception","IEnumerable","Action","Func","Span",
    "DateTime","TimeSpan","Guid"};

const QStringList kPyKeywords = {
    "False","None","True","and","as","assert","async","await","break","class","continue","def",
    "del","elif","else","except","finally","for","from","global","if","import","in","is",
    "lambda","nonlocal","not","or","pass","raise","return","try","while","with","yield","match","case"};
const QStringList kPyTypes = {"int","str","float","bool","list","dict","set","tuple","bytes",
    "object","self","cls","print","len","range","open","super","isinstance"};
const QStringList kCMakeKeywords = {
    "if","elseif","else","endif","foreach","endforeach","while","endwhile","function",
    "endfunction","macro","endmacro","return","break","continue","block","endblock"};
const QStringList kCMakeTypes = {
    "project","cmake_minimum_required","add_executable","add_library","add_subdirectory",
    "add_custom_command","add_custom_target","add_definitions","add_compile_options",
    "add_test","target_link_libraries","target_include_directories","target_compile_definitions",
    "target_compile_options","target_sources","target_compile_features","find_package",
    "find_library","find_path","find_program","include","include_directories","install","list",
    "message","option","set","unset","string","file","configure_file","set_target_properties",
    "set_property","get_property","get_filename_component","execute_process","enable_testing",
    "fetchcontent_declare","fetchcontent_makeavailable","qt_add_executable","qt_standard_project_setup"};
} // namespace

QStringList SyntaxHighlighter::builtinLanguages()
{
    return {QStringLiteral("C"), QStringLiteral("C++"), QStringLiteral("C#"), QStringLiteral("Markdown"),
            QStringLiteral("CMake"), QStringLiteral("Python"), QStringLiteral("JSON")};
}

QString SyntaxHighlighter::wordsRegex(const QStringList& w)
{
    return QStringLiteral("\\b(?:") + w.join(QLatin1Char('|')) + QStringLiteral(")\\b");
}

SyntaxHighlighter::SyntaxHighlighter(QTextDocument* doc, const QString& filePath)
    : QSyntaxHighlighter(doc)
{
    setupLanguage(filePath);
    buildFormats();
}

void SyntaxHighlighter::setupLanguage(const QString& filePath)
{
    const QFileInfo fi(filePath);
    const QString ext = fi.suffix().toLower();
    const QString name = fi.fileName().toLower();

    static const QStringList cppExt = {"cc","cpp","cxx","c++","h","hh","hpp","hxx","inl","ipp",
                                       "tpp","ixx","cppm","mm","m","cu","ino"};
    static const QStringList mdExt = {"md","markdown","mdown","mkd","mdx"};

    if (mdExt.contains(ext)) {
        m_lang.markdown = true;
        setupMarkdown();
        return;
    }

    if (ext == "c") {
        m_lang.keywords = kCKeywords;
        m_lang.types = kCTypes;
        m_lang.lineComment = "//";
        m_lang.blockStart = "/*";
        m_lang.blockEnd = "*/";
        m_lang.stringDelims = "\"'";
        m_lang.preproc = true;
    } else if (cppExt.contains(ext)) {
        m_lang.keywords = kCppKeywords;
        m_lang.types = kCppTypes;
        m_lang.lineComment = "//";
        m_lang.blockStart = "/*";
        m_lang.blockEnd = "*/";
        m_lang.stringDelims = "\"'";
        m_lang.preproc = true;
    } else if (ext == "cs" || ext == "csx") {
        m_lang.keywords = kCsKeywords;
        m_lang.types = kCsTypes;
        m_lang.lineComment = "//";
        m_lang.blockStart = "/*";
        m_lang.blockEnd = "*/";
        m_lang.stringDelims = "\"'";
        m_lang.preproc = true;   // #region, #if, #nullable ...
    } else if (ext == "py" || ext == "pyw") {
        m_lang.keywords = kPyKeywords;
        m_lang.types = kPyTypes;
        m_lang.lineComment = "#";
        m_lang.blockStart = "\"\"\"";
        m_lang.blockEnd = "\"\"\"";
        m_lang.stringDelims = "\"'";
    } else if (name == "cmakelists.txt" || ext == "cmake") {
        m_lang.keywords = kCMakeKeywords;
        m_lang.types = kCMakeTypes;
        m_lang.lineComment = "#";
        m_lang.blockStart = "#[[";
        m_lang.blockEnd = "]]";
        m_lang.caseInsensitive = true;
    } else if (ext == "json" || ext == "jsonc") {
        m_lang.keywords = {"true", "false", "null"};
        m_lang.lineComment = ext == "jsonc" ? "//" : "";
    }

    const auto opt = m_lang.caseInsensitive ? QRegularExpression::CaseInsensitiveOption
                                            : QRegularExpression::NoPatternOption;
    // Order matters: later rules override earlier ones.
    m_rules.append(Rule{QRegularExpression(QStringLiteral("\\b[A-Za-z_]\\w*(?=\\s*\\()")), Function});
    m_rules.append(Rule{QRegularExpression(QStringLiteral(
        "\\b(?:0[xX][0-9a-fA-F']+|\\d[\\d']*(?:\\.\\d+)?(?:[eE][+-]?\\d+)?[uUlLfFmMdD]*)\\b")), Number});
    if (!m_lang.types.isEmpty())
        m_rules.append(Rule{QRegularExpression(wordsRegex(m_lang.types), opt), Type});
    if (m_lang.preproc)
        m_rules.append(Rule{QRegularExpression(QStringLiteral("^\\s*#\\s*\\w+")), Preproc});
    if (!m_lang.keywords.isEmpty())
        m_rules.append(Rule{QRegularExpression(wordsRegex(m_lang.keywords), opt), Keyword});
    if (ext == "c" || cppExt.contains(ext))
        m_rules.append(Rule{QRegularExpression(QStringLiteral("\\b\\w+_t\\b")), Type});
}

void SyntaxHighlighter::setupMarkdown()
{
    const auto add = [this](const char* re, Kind k) {
        m_rules.append(Rule{QRegularExpression(QString::fromLatin1(re)), k});
    };
    add("^\\s*(?:[-*+]|\\d+[.)])\\s", Keyword);                                   // list markers
    add("\\b[A-Za-z][A-Za-z0-9+.-]*://[^\\s)>\\]]+", Link);                       // bare URLs
    add("(?<![*\\w])\\*[^*\\s][^*\\n]*\\*(?!\\*)|(?<![_\\w])_[^_\\s][^_\\n]*_(?![_\\w])", Italic);
    add("\\*\\*[^*\\n]+\\*\\*|__[^_\\n]+__", Bold);
    add("!?\\[[^\\]\\n]*\\]", Function);                                            // [text] / ![alt]
    add("\\]\\([^)\\n]*\\)|\\]\\[[^\\]\\n]*\\]", Link);                            // (url) / [ref]
    add("<https?://[^>\\n]+>", Link);
    add("`[^`\\n]+`", Code);                                                        // `inline code`
    add("^\\s{0,3}>.*$", Comment);                                                  // block quote
    add("^\\s{0,3}([-*_])(?:\\s*\\1){2,}\\s*$", Preproc);                          // horizontal rule
    add("^\\s{0,3}#{1,6}(?:\\s.*)?$", Heading);                                     // # headings
    add("^[=-]{3,}\\s*$", Heading);                                                 // setext underline
}

void SyntaxHighlighter::buildFormats()
{
    const ThemeColors& c = ThemeManager::instance().colors();
    const auto fmt = [](const QColor& col, bool bold = false, bool italic = false) {
        QTextCharFormat f;
        f.setForeground(col);
        if (bold) f.setFontWeight(QFont::Bold);
        f.setFontItalic(italic);
        return f;
    };
    m_fmt[Keyword]  = fmt(c.keyword);
    m_fmt[Type]     = fmt(c.type);
    m_fmt[String]   = fmt(c.string);
    m_fmt[Number]   = fmt(c.number);
    m_fmt[Comment]  = fmt(c.comment, false, true);
    m_fmt[Preproc]  = fmt(c.preproc);
    m_fmt[Function] = fmt(c.function);
    m_fmt[Heading]  = fmt(c.keyword, true);
    m_fmt[Bold]     = fmt(c.text, true);
    m_fmt[Italic]   = fmt(c.text, false, true);
    m_fmt[Code]     = fmt(c.string);
    m_fmt[Link]     = fmt(c.type);
}

void SyntaxHighlighter::highlightMarkdown(const QString& text)
{
    // Fenced code blocks (``` or ~~~) span lines: block state 2 = inside a fence.
    static const QRegularExpression fence(QStringLiteral("^\\s*(```|~~~)"));
    const bool inFence = previousBlockState() == 2;
    const int n = int(text.size());

    if (fence.match(text).hasMatch()) {
        setFormat(0, n, m_fmt[Preproc]);
        setCurrentBlockState(inFence ? 0 : 2);
        return;
    }
    if (inFence) {
        setFormat(0, n, m_fmt[Code]);
        setCurrentBlockState(2);
        return;
    }
    setCurrentBlockState(0);

    for (const Rule& r : std::as_const(m_rules)) {
        auto it = r.re.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            setFormat(int(m.capturedStart()), int(m.capturedLength()), m_fmt[r.kind]);
        }
    }
}

void SyntaxHighlighter::highlightBlock(const QString& text)
{
    if (m_lang.markdown) {
        highlightMarkdown(text);
        return;
    }

    for (const Rule& r : std::as_const(m_rules)) {
        auto it = r.re.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            setFormat(int(m.capturedStart()), int(m.capturedLength()), m_fmt[r.kind]);
        }
    }

    // Strings and comments override everything else; handled by a small scanner
    // so multi-line block comments carry over via block state.
    setCurrentBlockState(0);
    const int n = int(text.size());
    int i = 0;
    const bool hasBlock = !m_lang.blockStart.isEmpty();
    const QStringView view(text);

    if (hasBlock && previousBlockState() == 1) {
        const int end = int(text.indexOf(m_lang.blockEnd));
        if (end < 0) {
            setFormat(0, n, m_fmt[Comment]);
            setCurrentBlockState(1);
            return;
        }
        i = end + int(m_lang.blockEnd.size());
        setFormat(0, i, m_fmt[Comment]);
    }

    while (i < n) {
        if (hasBlock && view.mid(i).startsWith(m_lang.blockStart)) {
            const int from = i + int(m_lang.blockStart.size());
            const int end = int(text.indexOf(m_lang.blockEnd, from));
            if (end < 0) {
                setFormat(i, n - i, m_fmt[Comment]);
                setCurrentBlockState(1);
                return;
            }
            const int len = end + int(m_lang.blockEnd.size()) - i;
            setFormat(i, len, m_fmt[Comment]);
            i += len;
            continue;
        }
        if (!m_lang.lineComment.isEmpty() && view.mid(i).startsWith(m_lang.lineComment)) {
            setFormat(i, n - i, m_fmt[Comment]);
            return;
        }
        const QChar c = text.at(i);
        if (m_lang.stringDelims.contains(c)) {
            int j = i + 1;
            while (j < n) {
                if (text.at(j) == QLatin1Char('\\')) { j += 2; continue; }
                if (text.at(j) == c) { ++j; break; }
                ++j;
            }
            j = qMin(j, n);
            setFormat(i, j - i, m_fmt[String]);
            i = j;
            continue;
        }
        ++i;
    }
}
