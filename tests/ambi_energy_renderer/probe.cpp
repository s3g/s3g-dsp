// Standalone, non-installed GPU parity experiment. No CLAP/REAPER dependency.
#include "energy_gpu.h"
#include "s3g_ambisonic_utilities.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace s3g::energy;
namespace {
constexpr char fixtureMagic[8] = {'S','3','G','E','N','I','N','1'};
constexpr char goldenMagic[8] = {'S','3','G','E','N','M','T','1'};
constexpr float fieldMaxLimit = .003f;
constexpr double fieldMeanLimit = .00015;
constexpr int colorMaxLimit = 8;
constexpr double colorMeanLimit = .35;
struct Frame {
    char name[48]{};
    Params params;
    Snapshots samples{};
    uint32_t width = columns, height = rows;
    uint32_t preview = 0;
};
struct Fixture {
    std::vector<float> basis = std::vector<float>(pixels * channels);
    Weights weights{};
    std::vector<Frame> frames;
};
void require(bool pass, const std::string& message) {
    if (!pass) throw std::runtime_error(message);
}
template<class T> void write(std::ostream& out, const T* p, size_t count = 1) {
    out.write(reinterpret_cast<const char*>(p), std::streamsize(sizeof(T) * count));
    require(bool(out), "Could not write artifact");
}
template<class T> void read(std::istream& in, T* p, size_t count = 1) {
    in.read(reinterpret_cast<char*>(p), std::streamsize(sizeof(T) * count));
    require(bool(in), "Truncated or unreadable artifact");
}
uint64_t fingerprint(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    require(bool(in), "Cannot open input fixture");
    uint64_t hash = 14695981039346656037ull;
    char bytes[65536];
    while (in.read(bytes, sizeof(bytes)) || in.gcount())
        for (std::streamsize i = 0; i < in.gcount(); ++i) { hash ^= uint8_t(bytes[i]); hash *= 1099511628211ull; }
    require(in.eof(), "Cannot hash input fixture");
    return hash;
}
std::string fingerprintText(uint64_t hash) {
    std::ostringstream text;
    text << "Input fingerprint (FNV-1a64): " << std::hex << std::setfill('0') << std::setw(16) << hash;
    return text.str();
}
void checkHeader(std::istream& reference, uint64_t hash, uint32_t count) {
    char magic[8]; uint64_t expectedHash; uint32_t expectedCount;
    read(reference, magic, 8); read(reference, &expectedHash); read(reference, &expectedCount);
    require(!std::memcmp(magic, goldenMagic, 8) && hash == expectedHash && count == expectedCount,
            "Reference does not match these exact input snapshots/basis/parameters");
}
void name(Frame& frame, const std::string& text) {
    require(text.size() < sizeof(frame.name), "Fixture name too long");
    std::memset(frame.name, 0, sizeof(frame.name));
    std::memcpy(frame.name, text.data(), text.size());
}
void normalize(Frame& frame, const Weights& weights) {
    double full = 0, body = 0;
    for (uint32_t s = 0; s < frame.params.snapshotCount; ++s)
        for (uint32_t ch = 0; ch < frame.params.activeChannels; ++ch) {
            const double v = frame.samples[s * channels + ch];
            const double fw = weights[ch], bw = weights[channels + ch];
            full += v * v * fw * fw;
            if (ch < 9) body += v * v * bw * bw;
        }
    const double count = std::max(1u, frame.params.snapshotCount);
    frame.params.inverseFullRms = 1.f / std::max(.000001f, float(std::sqrt(full / count)));
    frame.params.inverseBodyRms = 1.f / std::max(.000001f, float(std::sqrt(body / count)));
}
Frame point(const Weights& weights, float azimuth, float elevation, uint32_t active = channels) {
    Frame f;
    f.params.snapshotCount = snapshotLimit; f.params.activeChannels = active;
    f.params.activity = .85f; f.params.directionFocus = .8f;
    const auto coefficients = s3g::acnSn3dBasis7(s3g::directionFromAed(azimuth, elevation));
    for (uint32_t s = 0; s < snapshotLimit; ++s) {
        // Signed, synchronized HOA samples. Independent channel RMS is NOT equivalent.
        const float amplitude = std::sin(float(s) * .63f + .2f) * .4f;
        for (uint32_t ch = 0; ch < active; ++ch) f.samples[s * channels + ch] = coefficients[ch] * amplitude;
    }
    normalize(f, weights);
    return f;
}
Fixture generate() {
    Fixture fixture;
    for (uint32_t y = 0; y < rows; ++y) for (uint32_t x = 0; x < columns; ++x) {
        const float az = 180.f - 360.f * (float(x) + .5f) / columns;
        const float el = -90.f + 180.f * (float(y) + .5f) / rows;
        const auto b = s3g::acnSn3dBasis7(s3g::directionFromAed(az, el));
        std::copy(b.begin(), b.end(), fixture.basis.begin() + (y * columns + x) * channels);
    }
    for (uint32_t ch = 0; ch < channels; ++ch) {
        const uint32_t order = uint32_t(std::sqrt(float(ch)));
        fixture.weights[ch] = s3g::ambiUtilityStandardOrderWeight(s3g::AmbiUtilityWeighting::MaxRe, order, 7);
        fixture.weights[channels + ch] = ch < 9 ?
            s3g::ambiUtilityStandardOrderWeight(s3g::AmbiUtilityWeighting::MaxRe, order, 2) : 0;
    }
    auto append = [&](Frame f, const std::string& text, bool preview = false) {
        name(f, text); f.preview = preview; fixture.frames.push_back(f);
    };
    Frame silence; silence.params.resetHistory = 1;
    append(silence, "empty-history", true);
    Frame mono = point(fixture.weights, 0, 0, 1);
    mono.params.snapshotCount = 1; normalize(mono, fixture.weights);
    append(mono, "one-channel-one-snapshot", true);
    const float az[] = {0, 45, 90, 179, -179, -90, -45, 0};
    for (unsigned i = 0; i < 8; ++i) {
        Frame f = point(fixture.weights, az[i], i < 4 ? 28.f : -37.f);
        f.params.resetHistory = i == 0; f.params.motionX = i < 4 ? .025f : -.025f;
        f.params.motionY = i < 4 ? .02f : -.02f;
        append(f, "moving-source-" + std::to_string(i), i == 3 || i == 4);
    }
    for (uint32_t active : {4u, 9u, 16u, 25u, 36u, 49u, 64u}) {
        Frame f = point(fixture.weights, 62, 19, active);
        f.params.resetHistory = 1;
        append(f, "active-channels-" + std::to_string(active));
    }
    Frame two = point(fixture.weights, -70, 35);
    const auto other = point(fixture.weights, 95, -28);
    for (size_t i = 0; i < two.samples.size(); ++i) two.samples[i] += other.samples[i] * (i / channels % 2 ? -.7f : .7f);
    normalize(two, fixture.weights);
    two.params.resetHistory = 1;
    append(two, "two-sources-opposed-phase", true);
    two.params.resetHistory = 0;
    for (unsigned i = 0; i < 5; ++i) append(two, "two-sources-settle-" + std::to_string(i));
    // Freeze the same field while testing all eight palette transfer functions.
    Frame palette = two;
    palette.params.bodyAttack = palette.params.bodyRelease = 0;
    palette.params.detailAttack = palette.params.detailRelease = 0;
    palette.params.wakeDecay = 1; palette.params.wakeGain = 0;
    const char* maps[] = {"field", "blue", "ink", "volt", "classic", "inferno", "viridis", "magma"};
    for (unsigned i = 0; i < 8; ++i) {
        palette.params.mapMode = i;
        palette.width = 513; palette.height = 257; // Non-integer scaling and padded GPU row pitches.
        append(palette, std::string("palette-") + maps[i], true);
    }
    palette.width = 1920; palette.height = 1080; append(palette, "large-1920x1080", true);
    palette.width = 319; palette.height = 193; append(palette, "resize-down-319x193", true);
    Frame decay; decay.params.snapshotCount = snapshotLimit;
    for (unsigned i = 0; i < 12; ++i) {
        decay.params.motionX = i % 2 ? -.04f : .04f;
        decay.params.motionY = i % 2 ? .04f : -.04f;
        append(decay, "silence-wake-" + std::to_string(i), i == 0 || i == 11);
    }
    // Exact half-pixel rounding, positive and negative, with nonzero wake history.
    decay.params.motionX = .5f / (float(columns) * .55f);
    decay.params.motionY = -.5f / (float(rows) * .55f);
    append(decay, "half-pixel-motion-positive");
    decay.params.motionX = -decay.params.motionX; decay.params.motionY = -decay.params.motionY;
    append(decay, "half-pixel-motion-negative");
    Frame high;
    high.params.snapshotCount = snapshotLimit; high.params.activity = .72f; high.params.resetHistory = 1;
    uint32_t rng = 0x12345678;
    for (unsigned s = 0; s < snapshotLimit; ++s) for (unsigned ch = 9; ch < channels; ++ch) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        high.samples[s * channels + ch] = (float(rng & 65535) / 32768.f - 1.f) * .2f;
    }
    normalize(high, fixture.weights);
    append(high, "higher-orders-no-body", true);
    silence.width = 257; silence.height = 129;
    append(silence, "reset-after-active", true);
    return fixture;
}
void saveFixture(const fs::path& path, const Fixture& fixture) {
    require(!fs::exists(path), "Refusing to overwrite existing fixture");
    std::ofstream out(path, std::ios::binary);
    const uint32_t count = uint32_t(fixture.frames.size());
    write(out, fixtureMagic, 8); write(out, &count);
    write(out, fixture.basis.data(), fixture.basis.size()); write(out, fixture.weights.data(), fixture.weights.size());
    for (const auto& f : fixture.frames) {
        write(out, f.name, sizeof(f.name)); write(out, &f.params);
        write(out, f.samples.data(), f.samples.size()); write(out, &f.width); write(out, &f.height); write(out, &f.preview);
    }
}
Fixture loadFixture(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    char magic[8]; uint32_t count;
    read(in, magic, 8); read(in, &count);
    require(!std::memcmp(magic, fixtureMagic, 8) && count >= 1 && count <= 128, "Invalid fixture header");
    Fixture fixture;
    read(in, fixture.basis.data(), fixture.basis.size()); read(in, fixture.weights.data(), fixture.weights.size());
    for (float v : fixture.basis) require(std::isfinite(v) && std::abs(v) <= 16, "Invalid basis value");
    for (float v : fixture.weights) require(std::isfinite(v) && std::abs(v) <= 1, "Invalid weight");
    fixture.frames.resize(count);
    for (auto& f : fixture.frames) {
        read(in, f.name, sizeof(f.name)); read(in, &f.params);
        read(in, f.samples.data(), f.samples.size()); read(in, &f.width); read(in, &f.height); read(in, &f.preview);
        require(f.name[sizeof(f.name) - 1] == 0 && f.name[0], "Invalid fixture name");
        for (const char* c = f.name; *c; ++c)
            require((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-', "Unsafe fixture name");
        require(f.preview <= 1, "Invalid preview flag");
        validate(f.params, f.samples, f.width, f.height);
    }
    require(fixture.frames.front().params.resetHistory == 1, "Fixture must initialize history");
    require(in.peek() == std::char_traits<char>::eof(), "Unexpected fixture trailing data");
    return fixture;
}
void checkFiles(const fs::path& input, const fs::path& golden) {
    const auto fixture = loadFixture(input);
    const uint64_t hash = fingerprint(input);
    std::ifstream reference(golden, std::ios::binary);
    checkHeader(reference, hash, uint32_t(fixture.frames.size()));
    uint64_t expectedSize = 20;
    for (const auto& frame : fixture.frames)
        expectedSize += uint64_t(pixels) * 8 + uint64_t(frame.width) * frame.height * 4;
    require(fs::file_size(golden) == expectedSize, "Wrong reference file size");
    std::cout << fingerprintText(hash) << "\nPASS: fixture/reference header and size match\n";
}
void bmp(const fs::path& path, const Capture& capture) {
    // Uncompressed 24-bit Windows bitmap: convenient to inspect on either OS.
    const uint32_t pitch = (capture.width * 3 + 3) & ~3u;
    const uint32_t size = 54 + pitch * capture.height;
    std::array<uint8_t, 54> header{};
    auto put = [&](size_t pos, uint32_t v) { for (unsigned i = 0; i < 4; ++i) header[pos + i] = uint8_t(v >> (8 * i)); };
    header[0] = 'B'; header[1] = 'M'; put(2, size); put(10, 54); put(14, 40);
    put(18, capture.width); put(22, capture.height); header[26] = 1; header[28] = 24;
    std::ofstream out(path, std::ios::binary); write(out, header.data(), header.size());
    std::vector<uint8_t> row(pitch);
    for (uint32_t y = capture.height; y-- > 0;) {
        for (uint32_t x = 0; x < capture.width; ++x)
            for (unsigned c = 0; c < 3; ++c) row[x * 3 + c] = capture.rgba[(size_t(y) * capture.width + x) * 4 + 2 - c];
        write(out, row.data(), row.size());
    }
}
struct Error { double mean = 0, maximum = 0; };
template<class T> Error difference(const std::vector<T>& a, const std::vector<T>& b, bool rgb = false) {
    require(a.size() == b.size(), "Capture size mismatch");
    Error result;
    size_t count = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        require(std::isfinite(double(a[i])) && std::isfinite(double(b[i])), "Non-finite GPU output");
        if (rgb && i % 4 == 3) { require(a[i] == 255 && b[i] == 255, "Invalid output alpha"); continue; }
        const double error = std::abs(double(a[i]) - double(b[i]));
        result.mean += error; result.maximum = std::max(result.maximum, error); ++count;
    }
    result.mean /= double(std::max(size_t(1), count));
    return result;
}
void selfTest() {
    for (uint32_t h = 0; h <= 65535; ++h) {
        if (((h >> 10) & 31) == 31 && (h & 1023)) continue;
        require(toHalf(fromHalf(uint16_t(h))) == h, "Half conversion roundtrip failed");
    }
    require(toHalf(1.f + 1.f / 2048) == 0x3c00, "Half ties-to-even failed");
    require(toHalf(1.f + 3.f / 2048) == 0x3c02, "Half odd-mantissa rounding failed");
    auto fixture = generate();
    std::vector<float> history(pixels * 4, .5f);
    const auto cleared = referenceField(fixture.frames.front().params, fixture.frames.front().samples,
        fixture.basis, fixture.weights, history);
    require(std::all_of(cleared.begin(), cleared.end(), [](float v) { return v == 0; }), "History reset failed");
    Frame mono = point(fixture.weights, 0, 0, 1); mono.params.resetHistory = 1;
    auto field = referenceField(mono.params, mono.samples, fixture.basis, fixture.weights, history);
    for (size_t i = 4; i < field.size(); ++i) require(field[i] == field[i % 4], "W-only field is not uniform");
    auto negative = mono;
    for (float& v : negative.samples) v = -v;
    require(referenceField(negative.params, negative.samples, fixture.basis, fixture.weights, history) == field,
            "Global polarity changed energy");
    auto high = fixture.frames[fixture.frames.size() - 2];
    field = referenceField(high.params, high.samples, fixture.basis, fixture.weights, history);
    bool detail = false;
    for (size_t i = 0; i < field.size(); i += 4) { require(field[i] == 0, "Higher orders leaked into body"); detail |= field[i + 1] > 0; }
    require(detail, "Higher-order detail missing");
    Frame wake;
    wake.params.motionX = -.04f; wake.params.motionY = .04f;
    std::fill(history.begin(), history.end(), 0.f);
    history[2] = .5f;
    field = referenceField(wake.params, wake.samples, fixture.basis, fixture.weights, history);
    const float decayed = fromHalf(toHalf(.5f * wake.params.wakeDecay));
    require(field[(2 * columns + columns - 2) * 4 + 2] == decayed, "Wake horizontal wrap failed");
    require(field[(columns - 2) * 4 + 2] == decayed, "Wake vertical clamp failed");
    require(field[(3 * columns + columns - 2) * 4 + 2] == 0, "Wake shifted to wrong row");
    auto invalid = mono.params; invalid.snapshotCount = snapshotLimit + 1;
    bool rejected = false;
    try { validate(invalid, mono.samples, columns, rows); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Invalid snapshot count accepted");
    std::cout << "PASS: half-float conversions, reset, W-only symmetry, polarity, higher-order isolation, wake wrap/clamp, validation\n";
}
int run(bool record, const fs::path& input, const fs::path& golden, const fs::path& output, bool software) {
#if !defined(__APPLE__)
    require(!record, "Golden references must be recorded from the shipping Metal shader on macOS");
#endif
    require(!fs::exists(output), "Choose a new output directory; previous results are preserved");
    if (record) require(!fs::exists(golden), "Refusing to overwrite existing Metal reference");
    fs::create_directories(output);
    std::ofstream report(output / "report.txt");
    require(bool(report), "Cannot create report");
    auto log = [&](const std::string& message) { std::cout << message << '\n'; report << message << '\n'; report.flush(); };
    try {
        auto fixture = loadFixture(input);
        const uint64_t hash = fingerprint(input);
        log(fingerprintText(hash));
        auto renderer = makeRenderer(fixture.basis, fixture.weights, software);
        log("Ambi Energy renderer prototype - NOT a CLAP plugin");
        log(renderer->deviceName());
        log("Field limits max=0.003 mean=0.00015; RGB limits max=8/255 mean=0.35/255 per frame.");
        std::fstream reference;
        reference.open(golden, std::ios::binary | (record ? std::ios::out : std::ios::in));
        require(bool(reference), "Cannot open Metal reference file");
        const uint32_t count = uint32_t(fixture.frames.size());
        if (record) {
            write(reference, goldenMagic, 8); write(reference, &hash); write(reference, &count);
        } else {
            checkHeader(reference, hash, count);
        }
        std::vector<float> cpuHistory(pixels * 4);
        bool passed = true;
        double renderTotal = 0, renderMax = 0;
        for (uint32_t i = 0; i < count; ++i) {
            const auto& frame = fixture.frames[i];
            const auto start = std::chrono::steady_clock::now();
            Capture actual = renderer->render(frame.params, frame.samples, frame.width, frame.height);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            renderTotal += ms; renderMax = std::max(renderMax, ms);
            cpuHistory = referenceField(frame.params, frame.samples, fixture.basis, fixture.weights, cpuHistory);
            const Error cpu = difference(actual.field, cpuHistory);
            bool framePass = cpu.maximum <= fieldMaxLimit && cpu.mean <= fieldMeanLimit;
            Capture expected;
            expected.width = frame.width; expected.height = frame.height;
            std::vector<uint16_t> half(pixels * 4);
            Error fields, colors;
            if (record) {
                for (size_t j = 0; j < half.size(); ++j) half[j] = toHalf(actual.field[j]);
                write(reference, half.data(), half.size()); write(reference, actual.rgba.data(), actual.rgba.size());
                for (size_t j = 3; j < actual.rgba.size(); j += 4) require(actual.rgba[j] == 255, "Non-opaque Metal output");
            } else {
                read(reference, half.data(), half.size());
                expected.field.resize(half.size()); expected.rgba.resize(actual.rgba.size());
                for (size_t j = 0; j < half.size(); ++j) expected.field[j] = fromHalf(half[j]);
                read(reference, expected.rgba.data(), expected.rgba.size());
                fields = difference(actual.field, expected.field);
                colors = difference(actual.rgba, expected.rgba, true);
                framePass &= fields.maximum <= fieldMaxLimit && fields.mean <= fieldMeanLimit &&
                             colors.maximum <= colorMaxLimit && colors.mean <= colorMeanLimit;
            }
            if (frame.preview || !framePass) {
                bmp(output / (std::string(frame.name) + ".bmp"), actual);
                if (!record) bmp(output / (std::string(frame.name) + "-metal-reference.bmp"), expected);
            }
            std::ostringstream line;
            line << (framePass ? "PASS " : "FAIL ") << frame.name << std::fixed << std::setprecision(6)
                 << " CPU-field max/mean=" << cpu.maximum << '/' << cpu.mean;
            if (!record) line << " Metal-field=" << fields.maximum << '/' << fields.mean
                             << " RGB=" << colors.maximum << '/' << colors.mean;
            line << std::setprecision(2) << " render+readback=" << ms << "ms";
            log(line.str()); passed &= framePass;
        }
        if (!record) require(reference.peek() == std::char_traits<char>::eof(), "Unexpected reference trailing data");
        else { reference.flush(); require(bool(reference), "Could not finish Metal reference"); }
        std::ostringstream timing;
        timing << "Synchronous render/readback mean=" << renderTotal / count << "ms max=" << renderMax
               << "ms (includes allocation/readback; NOT live GUI performance).";
        log(timing.str());
        log(std::string(passed ? "PASS: " : "FAIL: ") + std::to_string(count) + " frames checked.");
        if (!passed && record) log("DO NOT distribute this reference: CPU/Metal acceptance failed.");
        return passed ? 0 : 1;
    } catch (const std::exception& error) { log(std::string("ERROR: ") + error.what()); return 1; }
}
}
int runMain(int argc, char** argv) {
    try {
        const uint32_t endian = 1;
        require(*reinterpret_cast<const uint8_t*>(&endian) == 1 && sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                "Fixture format requires little-endian IEEE float32");
        if (argc == 2 && std::string(argv[1]) == "--self-test") { selfTest(); return 0; }
        if (argc == 4 && std::string(argv[1]) == "--check-files") {
            checkFiles(fs::u8path(argv[2]), fs::u8path(argv[3])); return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--generate") {
            auto fixture = generate(); saveFixture(fs::u8path(argv[2]), fixture);
            std::cout << "Wrote " << fixture.frames.size() << " deterministic renderer-input frames.\n";
            return 0;
        }
        if ((argc == 5 || argc == 6) && (std::string(argv[1]) == "--record" || std::string(argv[1]) == "--compare")) {
            require(argc == 5 || std::string(argv[5]) == "--software", "Unknown option");
            return run(std::string(argv[1]) == "--record", fs::u8path(argv[2]), fs::u8path(argv[3]), fs::u8path(argv[4]), argc == 6);
        }
        std::cerr << "Usage:\n  --self-test\n  --generate input.bin\n"
                     "  --check-files input.bin metal-reference.bin\n"
                     "  --record input.bin metal-reference.bin NEW-output-folder  (Mac only)\n"
                     "  --compare input.bin metal-reference.bin NEW-output-folder [--software]\n";
        return 2;
    } catch (const std::exception& error) { std::cerr << "ERROR: " << error.what() << '\n'; return 1; }
}
#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
    // Windows command lines are UTF-16. Convert explicitly before u8path;
    // narrow main() would corrupt paths outside the system ANSI code page.
    std::vector<std::string> utf8;
    std::vector<char*> arguments;
    for (int i = 0; i < argc; ++i) {
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr, 0, nullptr, nullptr);
        if (!size) { std::cerr << "Invalid UTF-16 command line\n"; return 2; }
        std::string text(size, '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, text.data(), size, nullptr, nullptr)) return 2;
        text.pop_back(); utf8.push_back(std::move(text));
    }
    for (auto& text : utf8) arguments.push_back(text.data());
    return runMain(argc, arguments.data());
}
#else
int main(int argc, char** argv) { return runMain(argc, argv); }
#endif
