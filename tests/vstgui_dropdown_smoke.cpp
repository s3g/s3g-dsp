// Pixel regression for the shared literal-port dropdown painter. Test the
// original item boundaries, not a separate reference-only menu implementation.
#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#endif
#include "../plugins/common/s3g_decoder_drawing.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <fstream>
#include <iostream>

namespace {
using namespace VSTGUI;
namespace F = s3g::portable_gui::foundation;
namespace D = s3g::portable_gui::ambi_effect_drawing;
bool ok = true;
void check(bool result, const std::string &message) {
  if (!result) {
    std::cerr << message << '\n';
    ok = false;
  }
}
class Menu : public s3g::portable_gui::routing::Canvas {
public:
  Menu() : Canvas(220., 160.) {}
  int hover = -1;
  bool decoder = false;
  bool withoutRules = false;
  void paint() override {
    D::DrawingScope scope(*this, context, font, titleFont);
    const D::String items[] = {"ONE", "TWO", "THREE", "FOUR", "FIVE"};
    const auto bounds = D::makeRect(20., 20., 180., 100.);
    if (withoutRules) {
      // Negative control: the same empty row backgrounds with NO rules.
      // At 65%, antialiasing can make a rule darker than the adjacent hover
      // fill. Compare against its actual background, not that hover color.
      fill(D::inset(bounds, -2., -2.), F::color(0x080808));
      fill(bounds, F::color(0x151515));
      stroke(bounds, F::color(0x6c6c6c));
      for (int row = 0; row < 5; ++row) {
        const auto b = F::rect(20., 20. + row * 20., 180., 20.);
        if (row == hover || row == 2)
          fill(CRect(b).inset(1., 1.),
               F::color(row == hover ? 0x343434 : 0x292929));
        else if (row % 2)
          fill(CRect(b).inset(1., 1.), F::palette().strip);
      }
      return;
    }
    if (decoder)
      s3g::portable_gui::decoder_drawing::drawDropdownMenu(
          bounds, 20., items, 5, 2, hover, D::softValueAttrs(),
          D::softTextStyle());
    else
      D::drawDropdownMenu(bounds, 20., items, 5, 2, hover, D::softValueAttrs(),
                          D::softTextStyle());
  }
};
void renderAndCheck(Menu &view, double scale) {
  const auto width = std::ceil(220. * scale), height = std::ceil(160. * scale);
  auto context = COffscreenContext::create({width, height});
  check(bool(context), "offscreen menu context");
  if (!context)
    return;
  context->beginDraw();
  {
    CDrawContext::Transform transform(*context,
                                      CGraphicsTransform().scale(scale, scale));
    view.draw(context);
  }
  context->endDraw();
  auto negative = COffscreenContext::create({width, height});
  check(bool(negative), "negative-control context");
  if (!negative)
    return;
  negative->beginDraw();
  {
    CDrawContext::Transform transform(*negative,
                                      CGraphicsTransform().scale(scale, scale));
    view.withoutRules = true;
    view.draw(negative);
    view.withoutRules = false;
  }
  negative->endDraw();
  auto pixels = owned(CBitmapPixelAccess::create(context->getBitmap()));
  auto negativePixels =
      owned(CBitmapPixelAccess::create(negative->getBitmap()));
  check(bool(pixels) && bool(negativePixels), "menu pixels readable");
  if (!pixels || !negativePixels)
    return;
  const auto redAt = [](CBitmapPixelAccess &source, int x, int y) {
    CColor pixel;
    source.setPosition(x, y);
    source.getColor(pixel);
    return int(pixel.red);
  };
  // Check three positions across every boundary, beyond the short labels and
  // away from the outer border/selection marker. Hover must not erase a rule.
  for (int row = 1; row < 5; ++row) {
    const double boundary = (20. + row * 20.) * scale;
    for (double logicalX : {120., 150., 180.}) {
      const int x = std::lround(logicalX * scale);
      int ruleContrast = 0;
      for (int y = int(std::floor(boundary)) - 1;
           y <= int(std::ceil(boundary)) + 1; ++y)
        ruleContrast = std::max(ruleContrast, redAt(*pixels, x, y) -
                                                  redAt(*negativePixels, x, y));
      check(ruleContrast > 2, "missing row separator at scale " +
                                  std::to_string(scale) + ", row " +
                                  std::to_string(row) + ", hover " +
                                  std::to_string(view.hover));
    }
  }
  pixels = nullptr;
  negativePixels = nullptr;
  if (const auto *root = std::getenv("S3G_DROPDOWN_CAPTURE_DIR")) {
    const auto directory = F::pathFromUtf8(root);
    std::filesystem::create_directories(directory);
    const auto name = std::string(view.decoder ? "decoder" : "shared") + "-" +
                      std::to_string(int(std::lround(scale * 100.))) +
                      "-hover-" + std::to_string(view.hover) + ".png";
    const auto png = getPlatformFactory().createBitmapMemoryPNGRepresentation(
        context->getBitmap()->getPlatformBitmap());
    std::ofstream file(directory / name, std::ios::binary);
    file.write(reinterpret_cast<const char *>(png.data()), png.size());
    check(bool(file), "menu capture written");
  }
}
void helperContract() {
  auto context = COffscreenContext::create({100., 80.});
  check(bool(context), "separator context");
  if (!context)
    return;
  context->beginDraw();
  context->setFillColor(F::color(0x151515));
  context->drawRect({0., 0., 100., 80.}, kDrawFilled);
  context->setLineWidth(7.);
  context->setFrameColor(F::color(0xabcdef));
  context->setLineStyle(kLineOnOffDash);
  F::drawDropdownItemSeparator(*context, {10., 20., 90., 40.}, 0);
  F::drawDropdownItemSeparator(*context, {10., 40., 90., 60.}, 1);
  check(context->getLineWidth() == 7., "separator restores prior line width");
  check(context->getFrameColor() == F::color(0xabcdef),
        "separator restores prior stroke color");
  check(context->getLineStyle() == kLineOnOffDash,
        "separator restores prior line style");
  context->endDraw();
  auto pixels = owned(CBitmapPixelAccess::create(context->getBitmap()));
  check(bool(pixels), "separator pixels");
  if (!pixels)
    return;
  CColor pixel;
  pixels->setPosition(50, 20);
  pixels->getColor(pixel);
  check(pixel.red == 0x15, "no internal separator above first item");
}
} // namespace

int main() {
#if defined(__APPLE__)
  [NSApplication sharedApplication];
#endif
  if (!F::acquireRuntime()) {
    std::cerr << "VSTGUI runtime unavailable\n";
    return 1;
  }
  {
    Menu view;
    for (bool decoder : {false, true}) {
      view.decoder = decoder;
      for (int hover : {-1, 0, 1, 2, 3, 4}) {
        view.hover = hover;
        for (double scale : {.65, 1., 1.25, 1.5, 2.})
          renderAndCheck(view, scale);
      }
    }
    helperContract();
    for (unsigned row = 0; row < 5; ++row)
      check(D::dropdownHitIndex({100., 20. + (row + .5) * 20.},
                                D::makeRect(20., 20., 180., 100.), 20.,
                                5) == int(row),
            "original menu row hit testing");
  }
  F::releaseRuntime();
  if (ok)
    std::cout << "Dropdown separators passed (60 renders, 65-200% scaling).\n";
  return ok ? 0 : 1;
}
