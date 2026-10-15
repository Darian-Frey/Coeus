#include "parser/CodeHighlighter.hpp"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace coeus {

namespace {

struct Language {
    QStringList lineComments;               // "//", "#", "--"
    QList<QPair<QString, QString>> blocks;  // block comments
    QString quotes;                         // string delimiters
    bool tripleQuotes = false;              // Python's """ and '''
    bool multilineQuotes = false;           // `…` spans lines
    bool preprocessor = false;              // C's #include at a line's start
    bool hashOnlyAfterSpace = false;        // shell: "#" starts a comment only at a word's start
    bool shellVariables = false;            // $NAME, ${NAME}
    bool callsAreKeywords = false;          // CMake: name( is a command
    bool caseInsensitive = false;           // SQL
    QSet<QString> keywords;
    QSet<QString> types;
};

QSet<QString> words(const char* list)
{
    QSet<QString> set;
    for (const QString& word : QString::fromLatin1(list).split(QLatin1Char(' '), Qt::SkipEmptyParts))
        set.insert(word);
    return set;
}

const QHash<QString, const Language*>& registry()
{
    static const Language c{
        {QStringLiteral("//")}, {{QStringLiteral("/*"), QStringLiteral("*/")}}, QStringLiteral("\"'"), false, false, true, false, false, false, false,
        words("alignas alignof asm break case catch class co_await co_return co_yield concept const consteval constexpr constinit "
              "const_cast continue decltype default delete do dynamic_cast else enum explicit export extern false for friend goto "
              "if inline mutable namespace new noexcept nullptr operator override final private protected public register "
              "reinterpret_cast requires return sizeof static static_assert static_cast struct switch template this thread_local "
              "throw true try typedef typeid typename union using virtual volatile while NULL"),
        words("auto bool char char8_t char16_t char32_t double float int long short signed unsigned void wchar_t size_t "
              "ssize_t ptrdiff_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t std string vector map "
              "unique_ptr shared_ptr optional expected QString QList QHash QObject QWidget")};
    static const Language python{
        {QStringLiteral("#")}, {}, QStringLiteral("\"'"), true, false, false, false, false, false, false,
        words("and as assert async await break class continue def del elif else except finally for from global if import in "
              "is lambda match case nonlocal not or pass raise return try while with yield True False None self"),
        words("int float str bytes bool list dict set tuple object type len range print open super isinstance enumerate zip "
              "map filter sorted Exception ValueError KeyError TypeError")};
    static const Language javascript{
        {QStringLiteral("//")}, {{QStringLiteral("/*"), QStringLiteral("*/")}}, QStringLiteral("\"'`"), false, true, false, false, false, false, false,
        words("async await break case catch class const continue debugger default delete do else export extends false finally "
              "for from function if import in instanceof let new null of return static super switch this throw true try "
              "typeof undefined var void while with yield as implements interface enum declare readonly type keyof"),
        words("string number boolean any unknown never object Array Map Set Promise Object String Number Boolean JSON Math "
              "console window document Error")};
    static const Language rust{
        {QStringLiteral("//")}, {{QStringLiteral("/*"), QStringLiteral("*/")}}, QStringLiteral("\""), false, false, false, false, false, false, false,
        words("as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move "
              "mut pub ref return self Self static struct super trait true type unsafe use where while"),
        words("i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char str String Vec Option Some None Result "
              "Ok Err Box Rc Arc HashMap")};
    static const Language go{
        {QStringLiteral("//")}, {{QStringLiteral("/*"), QStringLiteral("*/")}}, QStringLiteral("\"'`"), false, true, false, false, false, false, false,
        words("break case chan const continue default defer else fallthrough for func go goto if import interface map package "
              "range return select struct switch type var true false nil iota"),
        words("bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string uint uint8 uint16 "
              "uint32 uint64 uintptr any make new len cap append panic recover")};
    static const Language java{
        {QStringLiteral("//")}, {{QStringLiteral("/*"), QStringLiteral("*/")}}, QStringLiteral("\"'"), false, false, false, false, false, false, false,
        words("abstract assert break case catch class const continue default do else enum extends final finally for goto if "
              "implements import instanceof interface native new package private protected public return static strictfp super "
              "switch synchronized this throw throws transient try volatile while var val fun when object override internal "
              "namespace using get set async await true false null"),
        words("boolean byte char double float int long short void String Object Integer List Map Set Unit Any Int Boolean "
              "string bool decimal")};
    static const Language shell{
        {QStringLiteral("#")}, {}, QStringLiteral("\"'"), false, false, false, true, true, false, false,
        words("if then else elif fi for while until do done case esac in function return local export readonly declare set "
              "unset shift exit break continue source alias"),
        words("echo printf cd ls cat grep sed awk find mkdir rm cp mv git cmake make sudo apt python3 ctest test")};
    static const Language json{
        {}, {}, QStringLiteral("\""), false, false, false, false, false, false, false, words("true false null"), {}};
    static const Language yaml{
        {QStringLiteral("#")}, {}, QStringLiteral("\"'"), false, false, false, true, false, false, false,
        words("true false null yes no on off ~"), {}};
    static const Language toml{
        {QStringLiteral("#")}, {}, QStringLiteral("\"'"), false, false, false, false, false, false, false, words("true false"), {}};
    static const Language sql{
        {QStringLiteral("--")}, {{QStringLiteral("/*"), QStringLiteral("*/")}}, QStringLiteral("'\""), false, false, false, false, false, false, true,
        words("select from where and or not insert into values update set delete create table index view drop alter add "
              "primary key foreign references join left right inner outer on group by order having limit offset as distinct "
              "union all case when then else end exists in is null like between begin commit rollback transaction pragma "
              "default unique check if replace with recursive returning conflict do nothing"),
        words("integer int text real blob numeric boolean varchar char date datetime timestamp count sum min max avg "
              "coalesce")};
    static const Language cmake{
        {QStringLiteral("#")}, {}, QStringLiteral("\""), false, false, false, false, true, true, false,
        words("if elseif else endif foreach endforeach while endwhile function endfunction macro endmacro return"),
        words("ON OFF TRUE FALSE PUBLIC PRIVATE INTERFACE REQUIRED COMPONENTS CACHE STRING BOOL PATH FILEPATH")};

    static const QHash<QString, const Language*> languages{
        {QStringLiteral("c"), &c}, {QStringLiteral("cpp"), &c}, {QStringLiteral("c++"), &c}, {QStringLiteral("cc"), &c},
        {QStringLiteral("cxx"), &c}, {QStringLiteral("h"), &c}, {QStringLiteral("hpp"), &c}, {QStringLiteral("objc"), &c},
        {QStringLiteral("python"), &python}, {QStringLiteral("py"), &python},
        {QStringLiteral("javascript"), &javascript}, {QStringLiteral("js"), &javascript}, {QStringLiteral("jsx"), &javascript},
        {QStringLiteral("typescript"), &javascript}, {QStringLiteral("ts"), &javascript}, {QStringLiteral("tsx"), &javascript},
        {QStringLiteral("qml"), &javascript},
        {QStringLiteral("rust"), &rust}, {QStringLiteral("rs"), &rust},
        {QStringLiteral("go"), &go}, {QStringLiteral("golang"), &go},
        {QStringLiteral("java"), &java}, {QStringLiteral("kotlin"), &java}, {QStringLiteral("kt"), &java},
        {QStringLiteral("csharp"), &java}, {QStringLiteral("cs"), &java}, {QStringLiteral("c#"), &java},
        {QStringLiteral("bash"), &shell}, {QStringLiteral("sh"), &shell}, {QStringLiteral("shell"), &shell},
        {QStringLiteral("zsh"), &shell}, {QStringLiteral("console"), &shell},
        {QStringLiteral("json"), &json}, {QStringLiteral("jsonc"), &json},
        {QStringLiteral("yaml"), &yaml}, {QStringLiteral("yml"), &yaml},
        {QStringLiteral("toml"), &toml}, {QStringLiteral("ini"), &toml},
        {QStringLiteral("sql"), &sql}, {QStringLiteral("sqlite"), &sql},
        {QStringLiteral("cmake"), &cmake},
    };
    return languages;
}

QString span(const char* cls, const QString& text)
{
    return QStringLiteral("<span class=\"%1\">%2</span>").arg(QLatin1String(cls), text.toHtmlEscaped());
}

bool identifierChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

// One line's YAML key, or TOML key or [table], before the general pass.
qsizetype keyPrefix(const QString& code, qsizetype at, bool yaml, QString& html)
{
    static const QRegularExpression kYamlKey(QStringLiteral(R"(\G([ \t]*(?:- +)?)([^\s#:"'][^:#\n]*?)(:)(?=\s|$))"));
    static const QRegularExpression kTomlTable(QStringLiteral(R"(\G([ \t]*)(\[\[?[^\]\n]+\]\]?))"));
    static const QRegularExpression kTomlKey(QStringLiteral(R"(\G([ \t]*)([A-Za-z0-9_.\-"']+)([ \t]*=))"));
    if (yaml) {
        const auto m = kYamlKey.match(code, at, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
        if (!m.hasMatch())
            return at;
        html += m.captured(1).toHtmlEscaped() + span(CodeHighlighter::kType, m.captured(2)) + m.captured(3).toHtmlEscaped();
        return m.capturedEnd();
    }
    if (const auto m = kTomlTable.match(code, at, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
        m.hasMatch()) {
        html += m.captured(1).toHtmlEscaped() + span(CodeHighlighter::kKeyword, m.captured(2));
        return m.capturedEnd();
    }
    if (const auto m = kTomlKey.match(code, at, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
        m.hasMatch()) {
        html += m.captured(1).toHtmlEscaped() + span(CodeHighlighter::kType, m.captured(2)) + m.captured(3).toHtmlEscaped();
        return m.capturedEnd();
    }
    return at;
}

} // namespace

bool CodeHighlighter::knows(const QString& language)
{
    return registry().contains(language.trimmed().toLower());
}

QStringList CodeHighlighter::languages()
{
    QStringList names = registry().keys();
    names.sort();
    return names;
}

QString CodeHighlighter::toHtml(const QString& code, const QString& language)
{
    const QString name = language.trimmed().toLower();
    const Language* lang = registry().value(name);
    if (name == QLatin1String("diff") || name == QLatin1String("patch")) {
        // Whole lines: hunks, additions, removals.
        QString html;
        const QStringList lines = code.split(QLatin1Char('\n'));
        for (qsizetype i = 0; i < lines.size(); ++i) {
            const QString& line = lines[i];
            if (line.startsWith(QLatin1String("@@")) || line.startsWith(QLatin1String("diff ")))
                html += span(kKeyword, line);
            else if (line.startsWith(QLatin1Char('+')))
                html += span(kString, line);
            else if (line.startsWith(QLatin1Char('-')))
                html += span(kComment, line);
            else
                html += line.toHtmlEscaped();
            if (i + 1 < lines.size())
                html += QLatin1Char('\n');
        }
        return html;
    }
    if (!lang)
        return code.toHtmlEscaped();

    const bool yaml = name == QLatin1String("yaml") || name == QLatin1String("yml");
    const bool toml = name == QLatin1String("toml") || name == QLatin1String("ini");
    QString html;
    html.reserve(code.size() * 2);
    const qsizetype n = code.size();
    qsizetype i = 0;
    bool lineStart = true;
    while (i < n) {
        if (lineStart && (yaml || toml)) {
            i = keyPrefix(code, i, yaml, html);
            lineStart = false;
            if (i >= n)
                break;
        }
        const QChar ch = code[i];
        if (ch == QLatin1Char('\n')) {
            html += ch;
            ++i;
            lineStart = true;
            continue;
        }
        // Block comments.
        bool matched = false;
        for (const auto& [open, close] : lang->blocks) {
            if (code.mid(i, open.size()) == open) {
                const qsizetype end = code.indexOf(close, i + open.size());
                const qsizetype stop = end < 0 ? n : end + close.size();
                html += span(kComment, code.mid(i, stop - i));
                i = stop;
                matched = true;
                break;
            }
        }
        if (matched)
            continue;
        // C's preprocessor: "#include" at a line's start.
        if (lang->preprocessor && ch == QLatin1Char('#')) {
            qsizetype back = i - 1;
            while (back >= 0 && (code[back] == QLatin1Char(' ') || code[back] == QLatin1Char('\t')))
                --back;
            if (back < 0 || code[back] == QLatin1Char('\n')) {
                qsizetype end = i + 1;
                while (end < n && identifierChar(code[end]))
                    ++end;
                html += span(kKeyword, code.mid(i, end - i));
                i = end;
                continue;
            }
        }
        // Line comments.
        for (const QString& prefix : lang->lineComments) {
            if (code.mid(i, prefix.size()) != prefix)
                continue;
            if (lang->hashOnlyAfterSpace && i > 0 && !code[i - 1].isSpace())
                continue; // ${#array}, a#b
            const qsizetype end = code.indexOf(QLatin1Char('\n'), i);
            const qsizetype stop = end < 0 ? n : end;
            html += span(kComment, code.mid(i, stop - i));
            i = stop;
            matched = true;
            break;
        }
        if (matched)
            continue;
        // Strings.
        if (lang->quotes.contains(ch)) {
            if (lang->tripleQuotes && code.mid(i, 3) == QString(3, ch)) {
                const qsizetype end = code.indexOf(QString(3, ch), i + 3);
                const qsizetype stop = end < 0 ? n : end + 3;
                html += span(kString, code.mid(i, stop - i));
                i = stop;
                continue;
            }
            qsizetype end = i + 1;
            const bool spansLines = lang->multilineQuotes && ch == QLatin1Char('`');
            while (end < n && code[end] != ch && (spansLines || code[end] != QLatin1Char('\n'))) {
                if (code[end] == QLatin1Char('\\') && ch != QLatin1Char('`'))
                    ++end;
                ++end;
            }
            if (end < n && code[end] == ch)
                ++end;
            html += span(kString, code.mid(i, end - i));
            i = end;
            continue;
        }
        // Shell variables.
        if (lang->shellVariables && ch == QLatin1Char('$') && i + 1 < n) {
            qsizetype end = i + 1;
            if (code[end] == QLatin1Char('{')) {
                const qsizetype close = code.indexOf(QLatin1Char('}'), end);
                end = close < 0 ? n : close + 1;
            } else {
                while (end < n && identifierChar(code[end]))
                    ++end;
            }
            if (end > i + 1) {
                html += span(kType, code.mid(i, end - i));
                i = end;
                continue;
            }
        }
        // Numbers, not inside a name.
        if (ch.isDigit() && (i == 0 || !identifierChar(code[i - 1]))) {
            qsizetype end = i + 1;
            while (end < n && (code[end].isLetterOrNumber() || code[end] == QLatin1Char('.') || code[end] == QLatin1Char('_'))
                   && !(code[end] == QLatin1Char('.') && end + 1 < n && code[end + 1] == QLatin1Char('.')))
                ++end;
            html += span(kNumber, code.mid(i, end - i));
            i = end;
            continue;
        }
        // Words.
        if (ch.isLetter() || ch == QLatin1Char('_')) {
            qsizetype end = i + 1;
            while (end < n && identifierChar(code[end]))
                ++end;
            const QString word = code.mid(i, end - i);
            const QString key = lang->caseInsensitive ? word.toLower() : word;
            const bool call = lang->callsAreKeywords && end < n && code[end] == QLatin1Char('(');
            if (lang->keywords.contains(key) || call)
                html += span(kKeyword, word);
            else if (lang->types.contains(lang->caseInsensitive ? key : word))
                html += span(kType, word);
            else
                html += word.toHtmlEscaped();
            i = end;
            continue;
        }
        html += QString(ch).toHtmlEscaped();
        ++i;
    }
    return html;
}

} // namespace coeus
