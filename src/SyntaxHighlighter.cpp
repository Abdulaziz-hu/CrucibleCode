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

const QStringList kCppExt = {"cc","cpp","cxx","c++","h","hh","hpp","hxx","inl","ipp",
                             "tpp","ixx","cppm","mm","m","cu","ino"};

struct ExtraLang {
    QStringList exts, keywords, types;
    QString line, blockStart, blockEnd, delims;
    bool caseInsensitive = false;
    bool markup = false;
};

const QList<ExtraLang>& extraLanguages()
{
    static const QList<ExtraLang> langs = {
        {{"js","jsx","mjs","cjs","ts","tsx"},
         {"break","case","catch","class","const","continue","debugger","default","delete","do","else","export",
          "extends","finally","for","function","if","import","in","instanceof","let","new","of","return","super",
          "switch","this","throw","try","typeof","var","void","while","with","yield","async","await","static",
          "get","set","null","undefined","true","false","interface","type","enum","implements","namespace",
          "declare","readonly","abstract","as","from","public","private","protected"},
         {"number","string","boolean","any","unknown","never","object","Array","Promise","Map","Set","Date",
          "console","JSON","Math","Object","Error"},
         "//", "/*", "*/", "\"'`"},
        {{"java"},
         {"abstract","assert","break","case","catch","class","const","continue","default","do","else","enum",
          "extends","final","finally","for","goto","if","implements","import","instanceof","interface","native",
          "new","package","private","protected","public","return","static","strictfp","super","switch",
          "synchronized","this","throw","throws","transient","try","volatile","while","true","false","null",
          "var","record","sealed","permits"},
         {"boolean","byte","char","double","float","int","long","short","void","String","Integer","List","Map",
          "Set","Object","System"},
         "//", "/*", "*/", "\"'"},
        {{"kt","kts"},
         {"as","break","class","continue","do","else","false","for","fun","if","in","interface","is","null",
          "object","package","return","super","this","throw","true","try","typealias","val","var","when","while",
          "by","catch","constructor","finally","import","init","abstract","companion","const","data","enum",
          "final","inline","internal","lateinit","open","operator","override","private","protected","public",
          "sealed","suspend","vararg"},
         {"Int","Long","String","Boolean","Double","Float","Unit","Any","List","Map","Set","Nothing"},
         "//", "/*", "*/", "\"'"},
        {{"rs"},
         {"as","async","await","break","const","continue","crate","dyn","else","enum","extern","false","fn",
          "for","if","impl","in","let","loop","match","mod","move","mut","pub","ref","return","self","Self",
          "static","struct","super","trait","true","type","unsafe","use","where","while"},
         {"i8","i16","i32","i64","i128","isize","u8","u16","u32","u64","u128","usize","f32","f64","bool","char",
          "str","String","Vec","Option","Result","Box"},
         "//", "/*", "*/", "\""},
        {{"go"},
         {"break","case","chan","const","continue","default","defer","else","fallthrough","for","func","go",
          "goto","if","import","interface","map","package","range","return","select","struct","switch","type",
          "var","true","false","nil","iota"},
         {"bool","byte","rune","int","int8","int16","int32","int64","uint","uint8","uint16","uint32","uint64",
          "float32","float64","string","error","any"},
         "//", "/*", "*/", "\"'`"},
        {{"swift"},
         {"associatedtype","class","deinit","enum","extension","fileprivate","func","import","init","inout",
          "internal","let","open","operator","private","protocol","public","static","struct","subscript",
          "typealias","var","break","case","continue","default","defer","do","else","fallthrough","for","guard",
          "if","in","repeat","return","switch","where","while","as","Any","catch","false","is","nil","super",
          "self","Self","throw","throws","true","try","async","await","actor"},
         {"Int","Double","Float","Bool","String","Array","Dictionary","Set","Optional"},
         "//", "/*", "*/", "\""},
        {{"php"},
         {"abstract","and","as","break","callable","case","catch","class","clone","const","continue","declare",
          "default","do","echo","else","elseif","empty","extends","final","finally","fn","for","foreach",
          "function","global","if","implements","include","include_once","instanceof","interface","isset",
          "list","match","namespace","new","or","print","private","protected","public","readonly","require",
          "require_once","return","static","switch","throw","trait","try","unset","use","var","while","yield",
          "true","false","null"},
         {"int","float","string","bool","array","object","mixed","void"},
         "//", "/*", "*/", "\"'"},
        {{"rb"},
         {"alias","and","begin","break","case","class","def","do","else","elsif","end","ensure","false","for",
          "if","in","module","next","nil","not","or","redo","rescue","retry","return","self","super","then",
          "true","undef","unless","until","when","while","yield"},
         {"puts","require","attr_accessor","attr_reader","include","extend"},
         "#", "", "", "\"'"},
        {{"lua"},
         {"and","break","do","else","elseif","end","false","for","function","goto","if","in","local","nil",
          "not","or","repeat","return","then","true","until","while"},
         {"print","pairs","ipairs","require","table","string","math"},
         "--", "--[[", "]]", "\"'"},
        {{"sh","bash","zsh","fish","ksh"},
         {"if","then","else","elif","fi","for","while","until","do","done","case","esac","in","function","select",
          "time","return","exit","break","continue","local","export","readonly","declare","set","unset","source",
          "alias","echo","cd","test"},
         {},
         "#", "", "", "\"'"},
        {{"ps1","psm1"},
         {"begin","break","catch","class","continue","data","do","dynamicparam","else","elseif","end","exit",
          "filter","finally","for","foreach","from","function","if","in","param","process","return","switch",
          "throw","trap","try","until","using","while","$true","$false","$null"},
         {"Write-Host","Write-Output","Get-ChildItem","Set-Location","Get-Content","Out-File","Import-Module"},
         "#", "<#", "#>", "\"'", true},
        {{"sql"},
         {"select","from","where","insert","into","values","update","set","delete","create","table","alter",
          "drop","index","view","join","inner","left","right","outer","full","cross","on","group","by","order",
          "having","limit","offset","union","all","distinct","as","and","or","not","null","is","in","between",
          "like","exists","primary","key","foreign","references","default","unique","constraint","begin",
          "commit","rollback","case","when","then","else","end"},
         {"int","integer","varchar","char","text","date","datetime","timestamp","boolean","float","double",
          "decimal","numeric","bigint"},
         "--", "/*", "*/", "\"'", true},
        {{"yml","yaml"}, {"true","false","null","yes","no","on","off"}, {}, "#", "", "", "\"'"},
        {{"toml","ini","cfg","conf","properties"}, {"true","false"}, {}, "#", "", "", "\"'"},
        {{"css"}, {"important","inherit","initial","unset","none","auto","solid","dashed","dotted","flex","grid","block",
                   "inline","inline-block","absolute","relative","fixed","sticky","hidden","visible","center","bold","italic",
                   "transparent","pointer","wrap","nowrap","column","row","space-between","space-around","stretch","cover",
                   "contain","no-repeat","repeat","uppercase","lowercase","underline","normal","scroll"},
         {"color","background","background-color","background-image","background-size","background-position","margin",
          "margin-top","margin-right","margin-bottom","margin-left","padding","padding-top","padding-right",
          "padding-bottom","padding-left","border","border-radius","border-color","border-width","display","position",
          "top","right","bottom","left","width","height","min-width","max-width","min-height","max-height","font",
          "font-family","font-size","font-weight","line-height","text-align","text-decoration","text-transform",
          "letter-spacing","overflow","opacity","z-index","flex","flex-direction","flex-wrap","justify-content",
          "align-items","align-content","gap","grid-template-columns","grid-template-rows","grid-gap","transition",
          "transform","animation","box-shadow","cursor","content","float","clear","visibility","white-space","box-sizing"},
         "", "/*", "*/", "\"'"},
        {{"scss","less"}, {"important","inherit","initial","none","auto","@mixin","@include","@import","@extend"},
         {"color","background","margin","padding","border","display","position","width","height","font"},
         "//", "/*", "*/", "\"'"},
        {{"html","htm","xml","svg","xaml","csproj","vcxproj","props","targets","plist","xhtml","ui","qrc","axaml"},
         {"html","head","body","title","meta","link","script","style","div","span","p","a","img","ul","ol","li","table",
          "thead","tbody","tr","th","td","form","input","button","label","select","option","textarea","h1","h2","h3",
          "h4","h5","h6","header","footer","nav","main","section","article","aside","figure","figcaption","video",
          "audio","source","canvas","svg","iframe","br","hr","strong","em","code","pre","blockquote","template","slot",
          "class","id","href","src","alt","style","type","rel","charset","content","name","value","placeholder",
          "onclick","target","lang","viewport","stylesheet","defer","async","required","disabled","checked"},
         {}, "", "<!--", "-->", "\"'", false, true},
    };
    return langs;
}
} // namespace

QStringList SyntaxHighlighter::builtinLanguages()
{
    return {QStringLiteral("C"), QStringLiteral("C++"), QStringLiteral("C#"), QStringLiteral("Markdown"),
            QStringLiteral("CMake"), QStringLiteral("Python"), QStringLiteral("JSON"), QStringLiteral("JavaScript / TypeScript"),
            QStringLiteral("Java"), QStringLiteral("Kotlin"), QStringLiteral("Rust"), QStringLiteral("Go"),
            QStringLiteral("Swift"), QStringLiteral("PHP"), QStringLiteral("Ruby"), QStringLiteral("Lua"),
            QStringLiteral("Shell"), QStringLiteral("PowerShell"), QStringLiteral("SQL"), QStringLiteral("YAML / TOML / INI"),
            QStringLiteral("HTML / XML / CSS")};
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

SyntaxHighlighter::Lang SyntaxHighlighter::languageFor(const QString& filePath)
{
    Lang lang;
    const QFileInfo fi(filePath);
    const QString ext = fi.suffix().toLower();
    const QString name = fi.fileName().toLower();
    static const QStringList mdExt = {"md","markdown","mdown","mkd","mdx"};

    if (mdExt.contains(ext)) {
        lang.markdown = true;
        return lang;
    }
    if (ext == "c") {
        lang.keywords = kCKeywords;
        lang.types = kCTypes;
        lang.lineComment = "//";
        lang.blockStart = "/*";
        lang.blockEnd = "*/";
        lang.stringDelims = "\"'";
        lang.preproc = true;
    } else if (kCppExt.contains(ext)) {
        lang.keywords = kCppKeywords;
        lang.types = kCppTypes;
        lang.lineComment = "//";
        lang.blockStart = "/*";
        lang.blockEnd = "*/";
        lang.stringDelims = "\"'";
        lang.preproc = true;
    } else if (ext == "cs" || ext == "csx") {
        lang.keywords = kCsKeywords;
        lang.types = kCsTypes;
        lang.lineComment = "//";
        lang.blockStart = "/*";
        lang.blockEnd = "*/";
        lang.stringDelims = "\"'";
        lang.preproc = true;   // #region, #if, #nullable ...
    } else if (ext == "py" || ext == "pyw") {
        lang.keywords = kPyKeywords;
        lang.types = kPyTypes;
        lang.lineComment = "#";
        lang.blockStart = "\"\"\"";
        lang.blockEnd = "\"\"\"";
        lang.stringDelims = "\"'";
    } else if (name == "cmakelists.txt" || ext == "cmake") {
        lang.keywords = kCMakeKeywords;
        lang.types = kCMakeTypes;
        lang.lineComment = "#";
        lang.blockStart = "#[[";
        lang.blockEnd = "]]";
        lang.caseInsensitive = true;
    } else if (ext == "json" || ext == "jsonc") {
        lang.keywords = {"true", "false", "null"};
        lang.lineComment = ext == "jsonc" ? "//" : "";
    } else {
        for (const ExtraLang& x : extraLanguages()) {
            if (!x.exts.contains(ext)) continue;
            lang.keywords = x.keywords;
            lang.types = x.types;
            lang.lineComment = x.line;
            lang.blockStart = x.blockStart;
            lang.blockEnd = x.blockEnd;
            lang.stringDelims = x.delims;
            lang.caseInsensitive = x.caseInsensitive;
            lang.markup = x.markup;
            break;
        }
    }
    return lang;
}

QStringList SyntaxHighlighter::completionWords(const QString& filePath)
{
    const Lang l = languageFor(filePath);
    return l.keywords + l.types;
}

void SyntaxHighlighter::setupLanguage(const QString& filePath)
{
    m_lang = languageFor(filePath);
    if (m_lang.markdown) {
        setupMarkdown();
        return;
    }
    const QString ext = QFileInfo(filePath).suffix().toLower();

    const auto opt = m_lang.caseInsensitive ? QRegularExpression::CaseInsensitiveOption
                                            : QRegularExpression::NoPatternOption;
    if (m_lang.markup) {
        m_rules.append(Rule{QRegularExpression(QStringLiteral("\\b[\\w:.-]+(?==)")), Type});            // attributes
        m_rules.append(Rule{QRegularExpression(QStringLiteral("</?[A-Za-z][\\w:.-]*|/?>|\\?>|<\\?[A-Za-z]*")), Keyword});   // tags
        return;
    }
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
    if (ext == "c" || kCppExt.contains(ext))
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
