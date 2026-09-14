#include "s3g/tracker/command.h"
#include "s3g/tracker/editor_reference.h"
#include <iostream>

using namespace s3g::tracker;
using namespace s3g::tracker::editor;
int main()
{
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) {
            ++failures;
            std::cerr << message << '\n';
        }
    };
    const std::string unicode = "MIDI — café / 日本語 / 🥁";
    check(referenceUtf8(referenceUnicode(unicode)) == unicode, "UTF-8 scalar round trip");
    check(referenceUnicode("\xc0\xaf") == U"\ufffd\ufffd", "reject overlong UTF-8");
    check(referenceUnicode("\xed\xa0\x80") == U"\ufffd", "reject surrogate UTF-8");
    ConsoleModel console;
    std::vector<std::string> executed;
    console.execute = [&](const std::string& s) { executed.push_back(s); };
    console.draft = "unfinished";
    check(!console.submit(" \t\r\n") && executed.empty() && console.draft == "unfinished",
        "empty submission should not execute or clear draft");
    check(console.submit("\u00a0 bpm 120 \u2002"), "trim Unicode whitespace");
    check(executed == std::vector<std::string> { "bpm 120" } && console.draft.empty()
            && console.output.plainText() == "> : bpm 120\n",
        "command execution/log/draft");
    console.append(unicode, true);
    check(console.output.paragraphs.back().spans.front().style.rgb == 0xf06a72
            && console.output.plainText().find("! " + unicode) != std::string::npos,
        "Unicode error log");
    for (int i = 0; i < 110; ++i)
        console.submit("bpm " + std::to_string(i));
    check(console.history.size() == 100 && console.history.front() == "bpm 10"
            && console.history.back() == "bpm 109" && executed.size() == 111,
        "bounded 100-command history");
    ConsoleModel bounded;
    std::string expected;
    for (int i = 0; i < 220; ++i) {
        std::string line = std::to_string(i) + " " + std::string(170, 'x');
        bool error = i % 2;
        bounded.append(line, error);
        expected += std::string(error ? "! " : "> ") + line + '\n';
        if (expected.size() > 30000)
            expected.erase(0, 10000);
        check(bounded.output.plainText() == expected, "native 30k/10k log trimming");
    }
    check(consoleCompletions("PAN") == std::vector<std::string> { "panic" },
        "case-insensitive completion");
    check(consoleCompletions("fx") == std::vector<std::string>({ "fx", "fxvalue" }),
        "ambiguous completion order");
    check(consoleCompletions("not-a-command").empty() && consoleCompletions("").size() == 56,
        "full native completion table");
    auto help = trackerHelpDocument();
    auto text = help.plainText();
    for (const auto& section : CommandEngine::helpSections()) {
        check(text.find(section.title) != std::string::npos, "command reference section missing");
        for (const auto& entry : section.entries)
            check(text.find(entry.syntax) != std::string::npos
                    && text.find(entry.example) != std::string::npos,
                "command syntax/example missing");
    }
    for (auto* title : { "SEQUENCING ACTIONS", "TRACKER GRID WORKFLOW", "MIDI + LANE ROUTING",
             "TRANSPORT + SONG", "GEOMETRY + TOOL WINDOWS" })
        check(text.find(title) != std::string::npos, "native manual guide missing");
    ReferenceDocument doc;
    doc.paragraphs = { { { { "abc def gh", {} } } }, { { { unicode, {} } } } };
    ReferenceLayout layout;
    layout.build(
        doc, 32,
        [](const ReferenceStyle&) {
            return GridFont { "mono", 10, 8, 12 };
        },
        [](std::string_view, const ReferenceStyle&) { return 6.; });
    check(referenceUtf8(layout.text) == doc.plainText(), "wrapping must preserve exact text");
    check(layout.lines.size() > 3 && layout.glyphs[4].y == 12 && layout.glyphs[4].x == 0,
        "word wrapping geometry");
    check(layout.hit(1, 13) == 4 && layout.hit(13, 13) == 6, "wrapped text hit testing");
    auto word = layout.word(5);
    check(
        referenceUtf8(std::u32string_view(layout.text).substr(word.first, word.second - word.first))
            == "def",
        "word selection");
    auto paragraph = layout.paragraph(5);
    check(layout.word(3) == std::make_pair(std::size_t(3), std::size_t(4)),
        "double-click whitespace must not select its neighboring words");
    check(paragraph.first == 0 && paragraph.second == 11, "paragraph selection includes newline");
    if (!failures)
        std::cout << "Reference document / Console / Unicode / layout: ok\n";
    return failures ? 1 : 0;
}
