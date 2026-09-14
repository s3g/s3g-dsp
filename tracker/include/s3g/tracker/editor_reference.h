#pragma once
#include "s3g/tracker/editor_grid_painter.h"
#include <memory>

namespace s3g::tracker::editor {
std::u32string referenceUnicode(std::string_view);
std::string referenceUtf8(std::u32string_view);
struct ReferenceStyle {
    double size = 8.75;
    FontWeight weight = FontWeight::Regular;
    uint32_t rgb = 0xb2b7b8;
    double tracking = 0;
};
struct ReferenceSpan {
    std::string text;
    ReferenceStyle style;
};
struct ReferenceParagraph {
    std::vector<ReferenceSpan> spans;
    double before = 0, after = 0, spacing = 0, indent = 0, tail = 0;
};
struct ReferenceDocument {
    std::vector<ReferenceParagraph> paragraphs;
    std::string plainText() const;
};
ReferenceDocument trackerHelpDocument();
std::vector<std::string> consoleCompletions(std::string prefix);
// Shared by the main-page Live Code strip and detached Console. All state is
// UI-thread authoring/log state; executing a command remains a host callback.
struct ConsoleModel {
    std::string draft;
    std::vector<std::string> history;
    ReferenceDocument output;
    uint64_t revision = 0;
    std::function<void(const std::string&)> execute;
    void append(const std::string&, bool error = false);
    bool submit(std::string);
};

struct ReferenceGlyph {
    char32_t character = 0;
    std::size_t offset = 0, style = 0;
    double x = 0, y = 0, width = 0, height = 0, baseline = 0;
};
struct ReferenceLine {
    std::size_t first = 0, last = 0;
    double y = 0, height = 0;
};
// Portable wrapping, hit testing and selection use the same glyph positions as
// painting. Only font metrics and advances are supplied by the platform.
class ReferenceLayout {
public:
    std::u32string text;
    std::vector<ReferenceStyle> styles;
    std::vector<GridFont> fonts;
    std::vector<ReferenceGlyph> glyphs;
    std::vector<ReferenceLine> lines;
    double height = 0;
    void build(const ReferenceDocument&, double width,
        const std::function<GridFont(const ReferenceStyle&)>& font,
        const std::function<double(std::string_view, const ReferenceStyle&)>& advance,
        const std::function<GridFont(std::string_view, const ReferenceStyle&)>& fallback = {});
    std::size_t hit(double x, double y) const;
    std::pair<std::size_t, std::size_t> word(std::size_t offset) const;
    std::pair<std::size_t, std::size_t> paragraph(std::size_t offset) const;
};
}
