#pragma once
#include <vector>

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cview.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>

namespace s3g::portable_gui::foundation {

constexpr double kMinimumEditorScale = 0.65;
constexpr double kMaximumEditorScale = 2.0;

constexpr VSTGUI::CColor color(uint32_t rgb)
{
    return VSTGUI::CColor(
        static_cast<uint8_t>((rgb >> 16u) & 0xffu),
        static_cast<uint8_t>((rgb >> 8u) & 0xffu),
        static_cast<uint8_t>(rgb & 0xffu));
}

struct Palette {
    VSTGUI::CColor background = color(0x0d0d0d);
    VSTGUI::CColor strip = color(0x181818);
    VSTGUI::CColor cell = color(0x272727);
    VSTGUI::CColor grid = color(0x666666);
    VSTGUI::CColor dim = color(0xa0a0a0);
    VSTGUI::CColor text = color(0xd3d3d3);
    VSTGUI::CColor accent = color(0xc5c5c5);
    VSTGUI::CColor fill = color(0x919191);
    VSTGUI::CColor label = color(0xb7b7b7);
    VSTGUI::CColor value = color(0xa3a3a3);
    VSTGUI::CColor button = color(0x484848);
    VSTGUI::CColor buttonActive = color(0x636363);
};

struct FontMetrics {
    double body = 10.0;
    double title = 10.5;
    double channel = 8.0;
    double tiny = 7.0;
};

const Palette& palette();
const FontMetrics& fontMetrics();

// VSTGUI is initialized once per loaded CLAP module. A missing private font is
// deliberately non-fatal: makeUiFont() falls back to the platform monospace
// face, and finally to VSTGUI's system font.
bool acquireRuntime();
void releaseRuntime();
bool usingBundledFont();
const std::string& selectedFontFamily();
const std::filesystem::path& resourceDirectory();
VSTGUI::SharedPointer<VSTGUI::CFontDesc> makeUiFont(double size);

VSTGUI::CRect rect(double x, double y, double width, double height);
bool contains(const VSTGUI::CRect& bounds, const VSTGUI::CPoint& point);

void drawTextLine(VSTGUI::CDrawContext& context, const std::string& text,
    double x, double y, double width, VSTGUI::CColor textColor,
    VSTGUI::CFontRef font,
    VSTGUI::CHoriTxtAlign alignment = VSTGUI::kLeftText);
void drawTextInRect(VSTGUI::CDrawContext& context, const std::string& text,
    const VSTGUI::CRect& bounds, VSTGUI::CColor textColor,
    VSTGUI::CFontRef font,
    VSTGUI::CHoriTxtAlign alignment = VSTGUI::kCenterText);
// Presentation only: never use this spelling for CLAP descriptors, IDs, paths,
// or preset names. Keep the s3g brand lowercase and uppercase the ASCII name.
std::string pluginTitleText(std::string name);
void drawPluginTitle(VSTGUI::CDrawContext& context, const std::string& name,
    const VSTGUI::CRect& bounds, VSTGUI::CFontRef titleFont);
// Match Cocoa's bounded slider values: reduce numeric precision before clipping.
std::string sliderValueTextToFit(VSTGUI::CDrawContext& context,
    const std::string& value, double maximumWidth, VSTGUI::CFontRef font);
void drawPanel(VSTGUI::CDrawContext& context, const VSTGUI::CRect& bounds,
    const std::string& title, VSTGUI::CFontRef font,
    double headerHeight = 21.0, double labelInset = 8.0,
    double labelTop = 5.0);
void drawButton(VSTGUI::CDrawContext& context, const VSTGUI::CRect& bounds,
    const std::string& label, VSTGUI::CFontRef font, bool active = false,
    VSTGUI::CColor inactiveColor = color(0x484848));
void drawMenuBox(VSTGUI::CDrawContext& context,
    const VSTGUI::CRect& bounds, const std::string& value,
    VSTGUI::CFontRef font, const Palette& style = palette());
// Call after the row background/selection fill and before its text. Row zero
// has no internal rule; subsequent rows retain the original Cocoa separator.
inline void drawDropdownItemSeparator(VSTGUI::CDrawContext& context,
    const VSTGUI::CRect& row, uint32_t index,
    VSTGUI::CColor separator = color(0x3a3a3a))
{
    if (index == 0u) return;
    const auto previousWidth = context.getLineWidth();
    const auto previousColor = context.getFrameColor();
    const auto previousStyle = context.getLineStyle();
    context.setLineWidth(1.0);
    context.setLineStyle(VSTGUI::kLineSolid);
    context.setFrameColor(separator);
    context.drawLine({row.left, row.top}, {row.right, row.top});
    context.setFrameColor(previousColor);
    context.setLineWidth(previousWidth);
    context.setLineStyle(previousStyle);
}
void drawHorizontalSlider(VSTGUI::CDrawContext& context,
    const VSTGUI::CRect& track, double normalized,
    double handleTop, double handleHeight, const Palette& style = palette());

struct FileDialogOptions {
    bool save = false;
    bool directory = false;
    std::string title;
    std::string extensionDescription;
    std::string extension;
    std::string defaultSaveName;
    std::filesystem::path initialDirectory;
    // Optional additional suffixes for one file type (for example wav/wave).
    std::vector<std::string> extensions;
};

std::string runFileDialog(
    VSTGUI::CFrame* parent, const FileDialogOptions& options);
std::vector<std::string> runFileDialogs(
    VSTGUI::CFrame* parent, const FileDialogOptions& options);
std::filesystem::path presetDirectory(const char* pluginName);
std::string pathToUtf8(const std::filesystem::path& path);
std::filesystem::path pathFromUtf8(const char* path);
std::FILE* openFileUtf8(const char* path, const char* mode);

struct ParameterEditCallbacks {
    void* context = nullptr;
    void (*begin)(void*, uint32_t) = nullptr;
    void (*set)(void*, uint32_t, double) = nullptr;
    void (*end)(void*, uint32_t) = nullptr;
};

class ParameterEditSession {
public:
    ParameterEditSession() = default;
    explicit ParameterEditSession(
        ParameterEditCallbacks callbacks) : callbacks_(callbacks) {}
    ~ParameterEditSession();

    ParameterEditSession(const ParameterEditSession&) = delete;
    ParameterEditSession& operator=(const ParameterEditSession&) = delete;

    void setCallbacks(ParameterEditCallbacks callbacks);
    void begin(uint32_t parameterId);
    void set(double value);
    void end();
    void perform(uint32_t parameterId, double value);
    bool active() const { return parameterId_ != kNoParameter; }
    bool active(uint32_t parameterId) const
    {
        return parameterId_ == parameterId;
    }
    uint32_t parameterId() const { return parameterId_; }

private:
    static constexpr uint32_t kNoParameter =
        std::numeric_limits<uint32_t>::max();
    ParameterEditCallbacks callbacks_ {};
    uint32_t parameterId_ = kNoParameter;
};

class ContentView : public VSTGUI::CView {
public:
    explicit ContentView(const VSTGUI::CRect& size) : CView(size) {}
    virtual void startRefresh() {}
    virtual void stopRefresh() {}
    // Opt-in only: reflow into a non-proportional viewport while retaining
    // shared magnification. Existing fixed canvases remain unchanged.
    virtual bool hasResponsiveLayout() const { return false; }
};

class EditorHost {
public:
    EditorHost(uint32_t nativeWidth, uint32_t nativeHeight,
        uint32_t requestedWidth, uint32_t requestedHeight);
    virtual ~EditorHost();

    EditorHost(const EditorHost&) = delete;
    EditorHost& operator=(const EditorHost&) = delete;

    bool ready() const { return runtimeAcquired_; }
    bool attach(ContentView* contentView);
    bool setParent(void* nativeParent);
    bool setSize(uint32_t width, uint32_t height);
    bool setVisible(bool visible);

    // Main-thread editor-specific services (for example documentation capture).
    ContentView* contentView() const { return view_; }

protected:

private:
    void updateViewportScale();

    VSTGUI::CFrame* frame_ = nullptr;
    ContentView* view_ = nullptr;
    uint32_t nativeWidth_ = 0u;
    uint32_t nativeHeight_ = 0u;
    uint32_t width_ = 0u;
    uint32_t height_ = 0u;
    bool runtimeAcquired_ = false;
    bool opened_ = false;
};

} // namespace s3g::portable_gui::foundation
