#include "s3g/tracker/editor_reference.h"
#include "s3g/tracker/command.h"
#include "s3g/tracker/editor_grid.h"
#include "s3g/tracker/editor_palette.h"
#include "s3g/tracker/fx_catalog.h"
#include <cmath>

namespace s3g::tracker::editor {
std::u32string referenceUnicode(std::string_view bytes)
{
    std::u32string result;
    for (std::size_t i = 0; i < bytes.size();) {
        auto c = static_cast<unsigned char>(bytes[i++]);
        char32_t code = c;
        int continuation = 0;
        if (c >= 0xf0 && c < 0xf5) {
            code = c & 7;
            continuation = 3;
        } else if (c >= 0xe0 && c < 0xf0) {
            code = c & 15;
            continuation = 2;
        } else if (c >= 0xc2 && c < 0xe0) {
            code = c & 31;
            continuation = 1;
        } else if (c >= 0x80) {
            result.push_back(0xfffd);
            continue;
        }
        int count = continuation;
        bool valid = true;
        while (count--) {
            if (i == bytes.size() || (static_cast<unsigned char>(bytes[i]) & 0xc0) != 0x80) {
                valid = false;
                break;
            }
            code = (code << 6) | (static_cast<unsigned char>(bytes[i++]) & 63);
        }
        if (!valid || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)
            || (continuation == 1 && code < 0x80) || (continuation == 2 && code < 0x800)
            || (continuation == 3 && code < 0x10000))
            code = 0xfffd;
        result.push_back(code);
    }
    return result;
}
std::string referenceUtf8(std::u32string_view text)
{
    std::string result;
    for (auto c : text) {
        if (c < 0x80)
            result += char(c);
        else if (c < 0x800) {
            result += char(0xc0 | (c >> 6));
            result += char(0x80 | (c & 63));
        } else if (c < 0x10000) {
            result += char(0xe0 | (c >> 12));
            result += char(0x80 | ((c >> 6) & 63));
            result += char(0x80 | (c & 63));
        } else {
            result += char(0xf0 | (c >> 18));
            result += char(0x80 | ((c >> 12) & 63));
            result += char(0x80 | ((c >> 6) & 63));
            result += char(0x80 | (c & 63));
        }
    }
    return result;
}
std::string ReferenceDocument::plainText() const
{
    std::string result;
    for (const auto& p : paragraphs) {
        for (const auto& s : p.spans)
            result += s.text;
        result += '\n';
    }
    return result;
}
namespace {
    struct HelpGuide {
        std::string title;
        std::vector<std::pair<std::string, std::string>> entries;
    };
#include "editor_help_guides.inc"
}
ReferenceDocument trackerHelpDocument()
{
    ReferenceDocument doc;
    auto paragraph = [&](std::string text, ReferenceStyle style, double before = 0,
                         double after = 0, double spacing = 0, double indent = 0, double tail = 0) {
        doc.paragraphs.push_back(
            { { { std::move(text), style } }, before, after, spacing, indent, tail });
    };
    auto rule = [&](double before = 5) {
        paragraph(referenceUtf8(std::u32string(72, U'─')),
            { 7.5, FontWeight::Regular, nightNeutral(0x48), .15 }, before, 2);
    };
    const ReferenceStyle heading { 9.5, FontWeight::Semibold, 0xc0c0bc, .65 };
    const ReferenceStyle description { 8.75, FontWeight::Regular, nightNeutral(0xb5) };
    paragraph("Lane and row addresses are one-based. Targets accept a lane number or @alias. "
              "Commands are case-insensitive and invalid input leaves the session unchanged.",
        { 9, FontWeight::Regular, 0x92999b }, 0, 4, 1);
    for (const auto& section : CommandEngine::helpSections()) {
        rule();
        paragraph(std::string(section.title), heading, 1, 3, .5);
        for (const auto& entry : section.entries) {
            std::string text(entry.description);
            if (entry.syntax == "actions")
                text = "List the sequencing action keys accepted by SEQ1 and SEQ2.";
            else if (entry.syntax == "demo")
                text = "Load the General MIDI tracker demonstration.";
            paragraph(std::string(entry.syntax), { 9.5, FontWeight::Medium, 0x85cbd3 }, 2, 0, .5);
            paragraph(text, description, 0, 1, .75);
            doc.paragraphs.push_back(
                { { { "EXAMPLE  ", { 8.75, FontWeight::Medium, nightNeutral(0xb5) } },
                      { std::string(entry.example), { 8.75, FontWeight::Medium, 0xe8d47d } } },
                    0, 2, .75 });
        }
    }
    rule(7);
    paragraph("SEQUENCING ACTIONS", heading, 1, 3, .5);
    for (std::size_t i = 0; i < sequencerActionCount(); ++i)
        if (const auto* action = sequencerAction(i)) {
            std::string name(action->displayName);
            for (char& c : name)
                if (c >= 'a' && c <= 'z')
                    c = char(c - 'a' + 'A');
            paragraph(std::string(action->mnemonic) + "  " + name + "  —  "
                    + std::string(action->valueMeaning),
                description, 0, 1, .75);
        }
    paragraph(
        "CC0–CC127  MIDI CONTROL CHANGE  —  MIDI value 0–127; STEP or LINEAR between visited rows",
        description, 0, 1, .75);
    for (const auto& guide : helpGuides) {
        rule(7);
        paragraph(guide.title, heading, 1, 3, .5);
        for (const auto& entry : guide.entries) {
            paragraph(entry.first, { 8.5, FontWeight::Semibold, 0x85cbd3, .35 }, 2, 0, .25);
            paragraph(entry.second, { 8.25, FontWeight::Regular, 0x92999b }, 0, 2, .5, 10, 4);
        }
    }
    return doc;
}
std::vector<std::string> consoleCompletions(std::string prefix)
{
    constexpr const char* commands[] = { "help", "undo", "redo", "aliases", "alias", "kit", "play",
        "stop", "panic", "demo", "variation", "vary", "generate", "generateseed", "scene", "mutate",
        "drumscene", "bpm", "swing", "gate", "select", "hit", "rest", "repeat", "kill", "note",
        "vel", "velseq", "vol", "mask", "len", "stride", "dir", "mute", "unmute", "solo", "name",
        "eu", "euclid", "rotate", "fill", "reverse", "actions", "randomize", "random", "rand", "fx",
        "fxvalue", "interp", "interpolation", "warps", "warp", "out", "route", "instrument",
        "inst" };
    for (char& c : prefix)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    std::vector<std::string> result;
    for (auto* c : commands)
        if (std::string_view(c).substr(0, prefix.size()) == prefix)
            result.emplace_back(c);
    return result;
}
void ConsoleModel::append(const std::string& message, bool error)
{
    auto line = std::string(error ? "! " : "> ") + message;
    output.paragraphs.push_back(
        { { { line, { 8.5, FontWeight::Regular, error ? 0xf06a72u : 0xbababau } } } });
    // Match the native 30k UTF-16-unit bound / 10k prefix trim without splitting
    // a Unicode scalar. Keep each retained span's error/normal color.
    std::size_t units = 0;
    for (auto c : referenceUnicode(output.plainText()))
        units += c > 0xffff ? 2 : 1;
    if (units > 30000) {
        std::size_t remove = 10000;
        while (remove && !output.paragraphs.empty()) {
            auto& p = output.paragraphs.front();
            auto chars = referenceUnicode(p.spans.front().text);
            std::size_t n = 0;
            while (n < chars.size() && remove) {
                auto length = chars[n] > 0xffff ? 2u : 1u;
                remove = remove > length ? remove - length : 0;
                ++n;
            }
            if (n == chars.size() && remove) {
                --remove;
                output.paragraphs.erase(output.paragraphs.begin());
            } else {
                p.spans.front().text = referenceUtf8(std::u32string_view(chars).substr(n));
                break;
            }
        }
    }
    ++revision;
}
bool ConsoleModel::submit(std::string input)
{
    input = trimCellText(input);
    if (input.empty())
        return false;
    history.push_back(input);
    if (history.size() > 100)
        history.erase(history.begin());
    draft.clear();
    append(": " + input);
    if (execute)
        execute(input);
    return true;
}
void ReferenceLayout::build(const ReferenceDocument& doc, double width,
    const std::function<GridFont(const ReferenceStyle&)>& metric,
    const std::function<double(std::string_view, const ReferenceStyle&)>& advance,
    const std::function<GridFont(std::string_view, const ReferenceStyle&)>& fallback)
{
    text.clear();
    styles.clear();
    fonts.clear();
    glyphs.clear();
    lines.clear();
    height = 0;
    for (const auto& p : doc.paragraphs) {
        std::size_t begin = glyphs.size();
        for (const auto& span : p.spans) {
            std::size_t style = styles.size();
            styles.push_back(span.style);
            fonts.push_back(metric(span.style));
            for (char32_t c : referenceUnicode(span.text)) {
                auto offset = text.size();
                text += c;
                auto w = c == '\n' ? 0
                                   : advance(referenceUtf8(std::u32string_view(&c, 1)), span.style)
                        + span.style.tracking;
                auto glyphFont = c > 127 && fallback
                    ? fallback(referenceUtf8(std::u32string_view(&c, 1)), span.style)
                    : fonts.back();
                glyphs.push_back(
                    { c, offset, style, 0, 0, w, glyphFont.lineHeight, glyphFont.baseline });
            }
        }
        if (styles.empty()) {
            styles.push_back({});
            fonts.push_back(metric(styles.back()));
        }
        glyphs.push_back({ U'\n', text.size(), styles.size() - 1, 0, 0, 0, fonts.back().lineHeight,
            fonts.back().baseline });
        text += U'\n';
        height += p.before;
        while (begin < glyphs.size()) {
            std::size_t end = begin, breakAt = begin;
            double x = p.indent, lineHeight = 0, baseline = 0;
            while (end < glyphs.size()) {
                auto& g = glyphs[end];
                if (g.character == U'\n') {
                    ++end;
                    break;
                }
                // Cocoa permits trailing whitespace outside the line fragment;
                // wrapping before that space incorrectly moves the last word.
                if (g.character != U' ' && g.character != U'\t'
                    && x + g.width > std::max(p.indent + 1, width - p.tail) && end > begin) {
                    if (breakAt > begin)
                        end = breakAt;
                    break;
                }
                x += g.width;
                ++end;
                if (g.character == U' ' || g.character == U'\t' || g.character == U'-'
                    || g.character == U'–' || g.character == U'|')
                    breakAt = end;
            }
            for (auto i = begin; i < end; ++i) {
                lineHeight = std::max(lineHeight, glyphs[i].height);
                baseline = std::max(baseline, glyphs[i].baseline);
            }
            lineHeight += p.spacing;
            x = p.indent;
            for (auto i = begin; i < end; ++i) {
                auto& g = glyphs[i];
                g.x = x;
                g.y = height;
                g.height = lineHeight;
                g.baseline = height + baseline;
                x += g.width;
            }
            lines.push_back({ begin, end, height, lineHeight });
            height += lineHeight;
            begin = end;
        }
        height += p.after;
    }
}
std::size_t ReferenceLayout::hit(double x, double y) const
{
    if (lines.empty())
        return 0;
    const auto* line = &lines.back();
    for (const auto& l : lines)
        if (y < l.y + l.height) {
            line = &l;
            break;
        }
    for (auto i = line->first; i < line->last; ++i) {
        const auto& g = glyphs[i];
        if (g.character == U'\n' || x < g.x + g.width * .5)
            return g.offset;
    }
    return std::min(text.size(), glyphs[line->last - 1].offset + 1);
}
std::pair<std::size_t, std::size_t> ReferenceLayout::word(std::size_t at) const
{
    at = std::min(at, text.size());
    if (at == text.size())
        return { at, at };
    auto a = at, b = at;
    auto word = [](char32_t c) { return c > 32 && c != '\n'; };
    if (text[at] == U'\n')
        return { at, at + 1 };
    bool token = word(text[at]);
    while (a && text[a - 1] != U'\n' && word(text[a - 1]) == token)
        --a;
    while (b < text.size() && text[b] != U'\n' && word(text[b]) == token)
        ++b;
    return { a, b };
}
std::pair<std::size_t, std::size_t> ReferenceLayout::paragraph(std::size_t at) const
{
    at = std::min(at, text.size());
    auto a = at, b = at;
    while (a && text[a - 1] != U'\n')
        --a;
    while (b < text.size() && text[b] != U'\n')
        ++b;
    if (b < text.size())
        ++b;
    return { a, b };
}
}
