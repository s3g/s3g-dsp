#include "s3g_sample_cursor_presenter.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/iplatformframe.h"
#include <chrono>
#include <vector>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include "vstgui/lib/platform/mac/cgbitmap.h"

@interface S3GPortableSampleCursorOverlay : NSView
@property NSUInteger animationInstallCount;
- (NSUInteger)motionAnimationCount;
@end
@implementation S3GPortableSampleCursorOverlay
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return NO; }
- (NSView*)hitTest:(NSPoint)point { (void)point; return nil; }
- (NSUInteger)motionAnimationCount
{
    NSUInteger count = 0;
    for (CALayer* layer in self.layer.sublayers)
        if (!layer.hidden && [layer animationForKey:@"s3g.cursor.motion"]) ++count;
    return count;
}
@end
#elif defined(_WIN32)
#include <windows.h>
#include <d3d11.h>
#include <d2d1.h>
#include <dcomp.h>
#include <wrl/client.h>
#endif

namespace s3g::portable_gui {
using namespace VSTGUI;
namespace {
constexpr double kCursorOrigin = 99.0;
constexpr double kCursorWidth = 198.0;

SharedPointer<CBitmap> rasterCursor(const SampleCursorVisual& v,
    bool flagLeft, double backing)
{
    return renderBitmapOffscreen(CPoint(kCursorWidth, v.height), backing,
        [&](CDrawContext& dc) {
            dc.setDrawMode(kAntiAliasing);
            dc.setFrameColor(v.color);
            dc.setLineWidth(v.label.empty() ? 2.0 : 1.25);
            dc.drawLine(CPoint(kCursorOrigin, 0), CPoint(kCursorOrigin, v.height));
            if (v.label.empty()) return;
            auto font = foundation::makeUiFont(foundation::fontMetrics().channel);
            const double width = std::min(94.0,
                6.0 + static_cast<double>(v.label.size()) * foundation::fontMetrics().channel * 0.61);
            const auto box = foundation::rect(flagLeft
                    ? kCursorOrigin - width - 4 : kCursorOrigin + 4,
                v.flagTop, width, 14);
            dc.setFillColor(foundation::palette().background);
            dc.drawRect(box, kDrawFilled);
            foundation::drawTextInRect(dc, v.label, box, v.color, font);
        });
}

struct CursorCache {
    SampleCursorClock clock;
    SampleCursorVisual visual;
    bool painted = false, flagLeft = false;

    bool needsPaint(const SampleCursorVisual& v, bool left) const
    {
        return !painted || v.height != visual.height || v.color != visual.color
            || v.label != visual.label || v.flagTop != visual.flagTop || left != flagLeft;
    }
};

#if defined(__APPLE__)
class Presenter final : public SampleCursorPresenter {
public:
    explicit Presenter(CFrame* f) : frame(f)
    {
        NSView* parent = (__bridge NSView*)frame->getPlatformFrame()->getPlatformRepresentation();
        overlay = [[S3GPortableSampleCursorOverlay alloc] initWithFrame:NSZeroRect];
        CALayer* root = [CALayer layer];
        root.geometryFlipped = YES;
        root.masksToBounds = YES;
        root.backgroundColor = NSColor.clearColor.CGColor;
        overlay.layer = root;
        overlay.wantsLayer = YES;
        [parent addSubview:overlay];
        for (auto& layer : layers) {
            layer = [CALayer layer];
            layer.anchorPoint = CGPointZero;
            layer.hidden = YES;
            [root addSublayer:layer];
        }
    }
    ~Presenter() override { [overlay removeFromSuperview]; }

    bool update(const CRect& wave, const CRect& occlusion,
        double start, double span, const std::array<SampleCursorVisual, 64>& cursors,
        uint32_t count) override
    {
        const double scale = frame->getTransform().m11;
        const double backing = std::max(1.0, frame->getScaleFactor() * scale);
        const bool geometry = wave != lastWave || scale != lastScale
            || start != lastStart || span != lastSpan || backing != lastBacking;
        const double now = sampleCursorTime();
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        overlay.frame = NSMakeRect(wave.left * scale, wave.top * scale,
            wave.getWidth() * scale, wave.getHeight() * scale);
        overlay.bounds = NSMakeRect(0, 0, wave.getWidth(), wave.getHeight());
        if (occlusion.isEmpty()) overlay.layer.mask = nil;
        else {
            CAShapeLayer* mask = [CAShapeLayer layer];
            mask.fillRule = kCAFillRuleEvenOdd;
            CGMutablePathRef path = CGPathCreateMutable();
            CGPathAddRect(path, nullptr, overlay.bounds);
            CGPathAddRect(path, nullptr, CGRectMake(occlusion.left - wave.left,
                occlusion.top - wave.top, occlusion.getWidth(), occlusion.getHeight()));
            mask.path = path;
            CGPathRelease(path);
            overlay.layer.mask = mask;
        }
        for (uint32_t i = 0; i < layers.size(); ++i) {
            CALayer* layer = layers[i];
            auto& cache = caches[i];
            if (i >= count || cursors[i].trajectory.position < 0) {
                layer.hidden = YES;
                [layer removeAllAnimations];
                cache.clock = {};
                continue;
            }
            const auto& v = cursors[i];
            const bool changed = cache.clock.synchronize(v.trajectory, now);
            const double p = cache.clock.positionAt(now);
            const auto x = [&](double position) {
                return wave.getWidth() * (position - start) / span - kCursorOrigin;
            };
            const bool left = x(p) + kCursorOrigin > wave.getWidth() - 99;
            if (geometry || cache.needsPaint(v, left)) {
                const auto bitmap = rasterCursor(v, left, backing);
                if (!bitmap) { [CATransaction commit]; return false; }
                auto* cg = dynamic_cast<CGBitmap*>(bitmap->getPlatformBitmap().get());
                if (!cg) { [CATransaction commit]; return false; }
                layer.contents = (__bridge id)cg->getCGImage();
                layer.contentsScale = backing;
                layer.bounds = CGRectMake(0, 0, kCursorWidth, v.height);
                cache.painted = true;
                cache.flagLeft = left;
            }
            layer.hidden = NO;
            if (geometry || changed || v.top != cache.visual.top) {
                [layer removeAnimationForKey:@"s3g.cursor.motion"];
                layer.position = CGPointMake(x(p), v.top);
                const auto& t = cache.clock.trajectory();
                const double rate = cache.clock.rateAt(now);
                CABasicAnimation* animation = nil;
                if (t.smoothObserved && std::abs(p - t.position) > 1e-12) {
                    animation = [CABasicAnimation animationWithKeyPath:@"position.x"];
                    animation.fromValue = @(x(p));
                    animation.toValue = @(x(t.position));
                    animation.duration = 1.0 / 30.0;
                } else if (t.running && std::abs(rate) > 1e-12 && t.high > t.low) {
                    animation = [CABasicAnimation animationWithKeyPath:@"position.x"];
                    const double from = rate > 0 ? t.low : t.high;
                    const double to = rate > 0 ? t.high : t.low;
                    animation.fromValue = @(x(from));
                    animation.toValue = @(x(to));
                    animation.duration = (t.high - t.low) / std::abs(rate);
                    animation.beginTime = [layer convertTime:CACurrentMediaTime() fromLayer:nil]
                        - std::abs(p - from) / std::abs(rate);
                    if (t.loop) {
                        animation.repeatCount = HUGE_VALF;
                        animation.autoreverses = t.pingPong;
                    }
                }
                if (animation) {
                    animation.timingFunction = [CAMediaTimingFunction
                        functionWithName:kCAMediaTimingFunctionLinear];
                    animation.removedOnCompletion = NO;
                    animation.fillMode = kCAFillModeBoth;
                    [layer addAnimation:animation forKey:@"s3g.cursor.motion"];
                    overlay.animationInstallCount += 1;
                }
            }
            cache.visual = v;
        }
        [CATransaction commit];
        lastWave = wave; lastScale = scale; lastStart = start; lastSpan = span;
        lastBacking = backing;
        return true;
    }
private:
    CFrame* frame;
    S3GPortableSampleCursorOverlay* overlay;
    std::array<CALayer*, 64> layers {};
    std::array<CursorCache, 64> caches {};
    CRect lastWave;
    double lastScale = 0, lastStart = 0, lastSpan = 0, lastBacking = 0;
};
#elif defined(_WIN32)
using Microsoft::WRL::ComPtr;
class Presenter final : public SampleCursorPresenter {
public:
    explicit Presenter(CFrame* f) : frame(f) {}
    ~Presenter() override { if (window) DestroyWindow(window); }
    bool initialize()
    {
        ComPtr<ID3D11Device> d3d;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                &d3d, nullptr, nullptr))
            && FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                &d3d, nullptr, nullptr))) return false;
        ComPtr<IDXGIDevice> dxgi;
        if (FAILED(d3d.As(&dxgi)) || FAILED(DCompositionCreateDevice(dxgi.Get(),
                __uuidof(IDCompositionDevice), reinterpret_cast<void**>(device.GetAddressOf())))
            || FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf()))) return false;
        // STATIC belongs to Windows, so no class procedure can outlive this DLL.
        // Transparent hit testing leaves every drag/menu event with VSTGUI.
        HWND parent = static_cast<HWND>(frame->getPlatformFrame()->getPlatformRepresentation());
        window = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT
                | WS_EX_NOACTIVATE, L"STATIC", L"s3g cursor compositor",
            WS_CHILD | WS_VISIBLE | WS_DISABLED, 0, 0, 1, 1, parent, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (!window || FAILED(device->CreateTargetForHwnd(window, TRUE, &target))
            || FAILED(device->CreateVisual(&root)) || FAILED(target->SetRoot(root.Get()))) return false;
        for (auto& layer : layers) {
            if (FAILED(device->CreateVisual(&layer))
                || FAILED(root->AddVisual(layer.Get(), TRUE, nullptr))) return false;
        }
        return SUCCEEDED(device->Commit());
    }

    bool update(const CRect& wave, const CRect& occlusion,
        double start, double span, const std::array<SampleCursorVisual, 64>& cursors,
        uint32_t count) override
    {
        const double scale = frame->getTransform().m11 * frame->getScaleFactor();
        const bool geometry = wave != lastWave || scale != lastScale
            || start != lastStart || span != lastSpan;
        const double now = sampleCursorTime();
        if (geometry) SetWindowPos(window, HWND_TOP, static_cast<int>(std::lround(wave.left * scale)),
            static_cast<int>(std::lround(wave.top * scale)), static_cast<int>(std::ceil(wave.getWidth() * scale)),
            static_cast<int>(std::ceil(wave.getHeight() * scale)), SWP_NOACTIVATE);
        if (geometry || occlusion != lastOcclusion) {
            HRGN clip = CreateRectRgn(0, 0, static_cast<int>(std::ceil(wave.getWidth() * scale)),
                static_cast<int>(std::ceil(wave.getHeight() * scale)));
            if (!occlusion.isEmpty()) {
                HRGN hole = CreateRectRgn(static_cast<int>((occlusion.left - wave.left) * scale),
                    static_cast<int>((occlusion.top - wave.top) * scale),
                    static_cast<int>(std::ceil((occlusion.right - wave.left) * scale)),
                    static_cast<int>(std::ceil((occlusion.bottom - wave.top) * scale)));
                CombineRgn(clip, clip, hole, RGN_DIFF);
                DeleteObject(hole);
            }
            if (!SetWindowRgn(window, clip, TRUE)) DeleteObject(clip);
        }
        for (uint32_t i = 0; i < layers.size(); ++i) {
            auto* layer = layers[i].Get();
            auto& cache = caches[i];
            if (i >= count || cursors[i].trajectory.position < 0) {
                layer->SetContent(nullptr);
                cache.clock = {}; cache.painted = false;
                continue;
            }
            const auto& v = cursors[i];
            const bool changed = cache.clock.synchronize(v.trajectory, now);
            const double p = cache.clock.positionAt(now);
            const auto x = [&](double pos) {
                return static_cast<float>((wave.getWidth() * (pos - start) / span - kCursorOrigin) * scale);
            };
            const bool left = (p - start) / span * wave.getWidth() > wave.getWidth() - 99;
            if (geometry || cache.needsPaint(v, left)) {
                if (!paint(i, v, left, scale)) return false;
                cache.painted = true; cache.flagLeft = left;
            }
            if (geometry || changed || v.top != cache.visual.top) {
                layer->SetOffsetY(static_cast<float>(v.top * scale));
                layer->SetOffsetX(x(p));
                const auto& t = cache.clock.trajectory();
                const double rate = cache.clock.rateAt(now);
                ComPtr<IDCompositionAnimation> animation;
                if (t.smoothObserved && std::abs(p - t.position) > 1e-12) {
                    if (FAILED(device->CreateAnimation(&animation))) return false;
                    animation->AddCubic(0, x(p), (x(t.position) - x(p)) * 30.f, 0, 0);
                    animation->End(1.0 / 30.0, x(t.position));
                } else if (t.running && std::abs(rate) > 1e-12 && t.high > t.low) {
                    if (FAILED(device->CreateAnimation(&animation))) return false;
                    const double speed = std::abs(rate);
                    const double from = rate > 0 ? t.low : t.high;
                    const double to = rate > 0 ? t.high : t.low;
                    const double full = (t.high - t.low) / speed;
                    const double remaining = std::max(1e-6, std::abs(to - p) / speed);
                    const float slope = static_cast<float>(rate * wave.getWidth() / span * scale);
                    animation->AddCubic(0, x(p), slope, 0, 0);
                    if (!t.loop) animation->End(remaining, x(to));
                    else if (t.pingPong) {
                        animation->AddCubic(remaining, x(to), -slope, 0, 0);
                        animation->AddCubic(remaining + full, x(from), slope, 0, 0);
                        animation->AddRepeat(remaining + 2 * full, 2 * full);
                    } else {
                        animation->AddCubic(remaining, x(from), slope, 0, 0);
                        animation->AddRepeat(remaining + full, full);
                    }
                }
                if (animation && FAILED(layer->SetOffsetX(animation.Get()))) return false;
            }
            cache.visual = v;
        }
        lastWave = wave; lastOcclusion = occlusion;
        lastScale = scale; lastStart = start; lastSpan = span;
        return SUCCEEDED(device->Commit());
    }
private:
    bool paint(uint32_t index, const SampleCursorVisual& visual, bool left, double scale)
    {
        const auto bitmap = rasterCursor(visual, left, scale);
        if (!bitmap) return false;
        auto pixels = owned(CBitmapPixelAccess::create(bitmap));
        if (!pixels) return false;
        const uint32_t width = pixels->getBitmapWidth();
        const uint32_t height = pixels->getBitmapHeight();
        std::vector<uint8_t> bgra(width * height * 4u);
        for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
            CColor c {}; pixels->setPosition(x, y); pixels->getColor(c);
            const uint32_t offset = (y * width + x) * 4u;
            bgra[offset] = c.blue; bgra[offset + 1] = c.green;
            bgra[offset + 2] = c.red; bgra[offset + 3] = c.alpha;
        }
        ComPtr<IDCompositionSurface> surface;
        if (FAILED(device->CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
                DXGI_ALPHA_MODE_PREMULTIPLIED, &surface))) return false;
        ComPtr<IDXGISurface> dxgi;
        POINT offset {};
        if (FAILED(surface->BeginDraw(nullptr, __uuidof(IDXGISurface),
                reinterpret_cast<void**>(dxgi.GetAddressOf()), &offset))) return false;
        ComPtr<ID2D1RenderTarget> rt;
        const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        HRESULT hr = d2d->CreateDxgiSurfaceRenderTarget(dxgi.Get(), &props, &rt);
        if (SUCCEEDED(hr)) {
            ComPtr<ID2D1Bitmap> source;
            hr = rt->CreateBitmap(D2D1::SizeU(width, height), bgra.data(), width * 4u,
                D2D1::BitmapProperties(props.pixelFormat), &source);
            if (SUCCEEDED(hr)) {
                rt->BeginDraw();
                rt->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
                rt->PushAxisAlignedClip(D2D1::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)), D2D1_ANTIALIAS_MODE_ALIASED);
                rt->Clear(D2D1::ColorF(0, 0.0f));
                rt->DrawBitmap(source.Get());
                rt->PopAxisAlignedClip();
                hr = rt->EndDraw();
            }
        }
        const HRESULT end = surface->EndDraw();
        return SUCCEEDED(hr) && SUCCEEDED(end)
            && SUCCEEDED(layers[index]->SetContent(surface.Get()));
    }
    CFrame* frame;
    HWND window = nullptr;
    ComPtr<IDCompositionDevice> device;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> root;
    ComPtr<ID2D1Factory> d2d;
    std::array<ComPtr<IDCompositionVisual>, 64> layers;
    std::array<CursorCache, 64> caches;
    CRect lastWave, lastOcclusion;
    double lastScale = 0, lastStart = 0, lastSpan = 0;
};
#endif
} // namespace

double sampleCursorTime()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool sampleCursorScreenDraw()
{
#if defined(__APPLE__)
    return [[NSGraphicsContext currentContext] isDrawingToScreen];
#else
    return true;
#endif
}

std::unique_ptr<SampleCursorPresenter> SampleCursorPresenter::create(CFrame* frame)
{
    if (!frame || !frame->getPlatformFrame()) return {};
#if defined(__APPLE__) || defined(_WIN32)
    auto result = std::make_unique<Presenter>(frame);
#if defined(_WIN32)
    if (!result->initialize()) return {};
#endif
    return result;
#else
    return {};
#endif
}
} // namespace s3g::portable_gui
