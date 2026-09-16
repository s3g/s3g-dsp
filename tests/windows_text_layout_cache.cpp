#define NOMINMAX
#include "s3g_windows_text_layout_cache.h"
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>
using Microsoft::WRL::ComPtr;
using s3g::windows_gui::TextLayoutCache;
static unsigned checks = 0;
static void require(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
static ComPtr<IDWriteTextFormat> makeFormat(IDWriteFactory* factory,
    const wchar_t* family, float size, bool bold = false) {
    ComPtr<IDWriteTextFormat> format;
    require(SUCCEEDED(factory->CreateTextFormat(family, nullptr,
        bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &format)), "format");
    return format;
}
static ComPtr<IDWriteTextLayout> uncached(IDWriteFactory* factory,
    IDWriteTextFormat* format, const wchar_t* text, bool underline, bool strike) {
    ComPtr<IDWriteTextLayout> layout;
    require(SUCCEEDED(factory->CreateTextLayout(text, static_cast<UINT32>(std::wcslen(text)),
        format, 10000, 1000, &layout)), "layout");
    DWRITE_TEXT_RANGE range {0, UINT32_MAX};
    if (underline) layout->SetUnderline(true, range);
    if (strike) layout->SetStrikethrough(true, range);
    return layout;
}
static ComPtr<IDWriteTextLayout> cached(TextLayoutCache& cache, IDWriteFactory* factory,
    IDWriteTextFormat* format, const wchar_t* text, bool underline = false, bool strike = false) {
    ComPtr<IDWriteTextLayout> layout;
    layout.Attach(cache.get(factory, format, text, underline, strike));
    require(layout != nullptr, "cached layout");
    return layout;
}
static std::vector<unsigned char> pixels(IWICImagingFactory* wic, ID2D1Factory* d2d,
    IDWriteTextLayout* layout, float scale, bool antialias) {
    ComPtr<IWICBitmap> bitmap;
    require(SUCCEEDED(wic->CreateBitmap(480, 144, GUID_WICPixelFormat32bppPBGRA,
        WICBitmapCacheOnLoad, &bitmap)), "WIC bitmap");
    ComPtr<ID2D1RenderTarget> target;
    auto props = D2D1::RenderTargetProperties();
    require(SUCCEEDED(d2d->CreateWicBitmapRenderTarget(bitmap.Get(), props, &target)), "render target");
    ComPtr<ID2D1SolidColorBrush> brush;
    require(SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(0.7f, 0.9f, 0.8f), &brush)), "brush");
    target->BeginDraw();
    target->Clear(D2D1::ColorF(0.05f, 0.05f, 0.05f));
    target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
    target->SetTextAntialiasMode(antialias ? D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE : D2D1_TEXT_ANTIALIAS_MODE_ALIASED);
    target->PushAxisAlignedClip(D2D1::RectF(3, 2, 238, 70), D2D1_ANTIALIAS_MODE_ALIASED);
    target->DrawTextLayout(D2D1::Point2F(4.5f, 4.25f), layout, brush.Get());
    target->PopAxisAlignedClip();
    require(SUCCEEDED(target->EndDraw()), "draw");
    std::vector<unsigned char> bytes(480 * 144 * 4);
    require(SUCCEEDED(bitmap->CopyPixels(nullptr, 480 * 4, static_cast<UINT>(bytes.size()), bytes.data())), "pixels");
    return bytes;
}
static void benchmark(IDWriteFactory* factory) {
    auto format = makeFormat(factory, L"Consolas", 11);
    TextLayoutCache cache;
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    double checksum[2] {};
    std::puts("frame,uncached_ms,cached_ms");
    for (unsigned frame = 0; frame < 180; ++frame) {
        double times[2] {};
        for (unsigned run = 0; run < 2; ++run) {
            const auto mode = (frame + run) & 1u;
            LARGE_INTEGER start, end; QueryPerformanceCounter(&start);
            for (unsigned label = 0; label < 192; ++label) {
                const auto text = label < 144 ? L"Control label " + std::to_wstring(label)
                    : std::to_wstring(frame * 0.01 + label);
                auto layoutFor = [&] {
                    return mode ? cached(cache, factory, format.Get(), text.c_str())
                        : uncached(factory, format.Get(), text.c_str(), false, false);
                };
                if (label % 3) {
                    auto layout = layoutFor(); DWRITE_TEXT_METRICS m {};
                    require(SUCCEEDED(layout->GetMetrics(&m)), "benchmark metrics");
                    checksum[mode] += m.widthIncludingTrailingWhitespace;
                }
                auto layout = layoutFor(); DWRITE_LINE_METRICS line {}; UINT32 n = 0;
                require(SUCCEEDED(layout->GetLineMetrics(&line, 1, &n)), "benchmark line");
                checksum[mode] += line.baseline;
            }
            QueryPerformanceCounter(&end);
            times[mode] = 1000.0 * (end.QuadPart - start.QuadPart) / frequency.QuadPart;
        }
        std::printf("%u,%.9f,%.9f\n", frame, times[0], times[1]);
    }
    require(checksum[0] == checksum[1], "benchmark output changed");
    std::fprintf(stderr, "matched metric checksum %.9f; cache entries %zu; layout-only diagnostic\n", checksum[0], cache.size());
}
int main(int argc, char** argv) {
    const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(apartment)) return 2;
    int result = 0;
    try {
        ComPtr<IDWriteFactory> factory;
        require(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(factory.GetAddressOf()))), "DWrite factory");
        if (argc > 1 && std::strcmp(argv[1], "--benchmark") == 0) benchmark(factory.Get());
        else {
            ComPtr<IWICImagingFactory> wic;
            require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&wic))), "WIC factory");
            ComPtr<ID2D1Factory> d2d;
            require(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())), "D2D factory");
            TextLayoutCache cache;
            const wchar_t* texts[] = {L"", L"Order 7 / 64 voices  ", L"0.0000123 dB", L"Copy\tPaste\nNext line",
                L"\u00c9tage \u03a9 \u4e2d\u6587 \U0001f30d", L"\u05e9\u05dc\u05d5\u05dd 123", L"e\u0301 \u2192 [live code]"};
            unsigned images = 0;
            for (const auto* family : {L"Consolas", L"Segoe UI", L"Arial"}) {
                for (float size : {8.0f, 11.0f, 17.5f}) {
                    auto format = makeFormat(factory.Get(), family, size, size > 15);
                    for (unsigned style = 0; style < 4; ++style) for (const auto* text : texts) {
                        const auto a = uncached(factory.Get(), format.Get(), text, style & 1, style & 2);
                        const auto b = cached(cache, factory.Get(), format.Get(), text, style & 1, style & 2);
                        const auto hit = cached(cache, factory.Get(), format.Get(), text, style & 1, style & 2);
                        require(hit.Get() == b.Get(), "expected cache hit");
                        DWRITE_TEXT_METRICS am {}, bm {};
                        require(SUCCEEDED(a->GetMetrics(&am)) && SUCCEEDED(b->GetMetrics(&bm)), "metrics");
                        require(std::memcmp(&am, &bm, sizeof am) == 0, "text metrics changed");
                        for (float scale : {0.65f, 1.0f, 2.0f}) {
                            const bool antialias = (style & 1) == 0;
                            require(pixels(wic.Get(), d2d.Get(), a.Get(), scale, antialias)
                                == pixels(wic.Get(), d2d.Get(), b.Get(), scale, antialias), "pixels changed");
                            ++images;
                        }
                    }
                    cache.erase(format.Get());
                    require(cache.size() == 0, "font destruction retained layouts");
                }
            }
            auto format = makeFormat(factory.Get(), L"Consolas", 11);
            const auto retained = cached(cache, factory.Get(), format.Get(), L"retained caller reference");
            for (unsigned n = 0; n < 1024; ++n)
                cached(cache, factory.Get(), format.Get(), std::to_wstring(n).c_str());
            require(cache.size() == TextLayoutCache::capacity, "cache capacity");
            DWRITE_TEXT_METRICS metrics {};
            require(SUCCEEDED(retained->GetMetrics(&metrics)), "eviction invalidated caller");
            std::wstring longText(TextLayoutCache::maximumTextLength + 1, L'x');
            auto longA = cached(cache, factory.Get(), format.Get(), longText.c_str());
            auto longB = cached(cache, factory.Get(), format.Get(), longText.c_str());
            require(longA.Get() != longB.Get(), "long text should bypass cache");
            require(cache.size() == TextLayoutCache::capacity, "long text consumed cache");
            cache.clear();
            require(cache.size() == 0 && SUCCEEDED(retained->GetMetrics(&metrics)), "clear lifetime");
            std::atomic<unsigned> errors {0};
            std::vector<std::thread> workers;
            for (unsigned thread = 0; thread < 4; ++thread) workers.emplace_back([&] {
                for (unsigned n = 0; n < 1000; ++n) {
                    auto* layout = cache.get(factory.Get(), format.Get(), std::to_wstring(n % 64).c_str(), false, false);
                    DWRITE_TEXT_METRICS m {};
                    if (!layout || FAILED(layout->GetMetrics(&m))) ++errors;
                    if (layout) layout->Release();
                }
            });
            for (auto& worker : workers) worker.join();
            require(errors == 0 && cache.size() == 64, "concurrent access");
            cache.erase(format.Get());
            require(cache.size() == 0, "final cleanup");
            std::printf("PASS: %u checks; %u pixel-identical render pairs; 4000 concurrent lookups; bounded cache and caller lifetime\n", checks, images);
        }
    } catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); result = 1; }
    CoUninitialize();
    return result;
}
