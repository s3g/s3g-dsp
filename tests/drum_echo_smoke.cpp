#include "s3g_drum_echo.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

float renderEchoPeak(const s3g::DrumEchoParams& params,
    uint32_t impulseFrame, uint32_t observationStart,
    uint32_t observationEnd, bool secondImpulse = false)
{
    s3g::DrumEcho echo;
    echo.setParams(params);
    echo.prepare(1000.0, 12.0);
    echo.setTempo(120.0, true);
    echo.reset();
    float peak = 0.0f;
    for (uint32_t frame = 0u; frame < observationEnd; ++frame) {
        const bool impulse = frame == impulseFrame
            || (secondImpulse && frame == observationStart);
        float left = impulse ? 1.0f : 0.0f;
        float right = left;
        echo.processFrame(left, right);
        if (frame >= observationStart) {
            peak = std::max(peak, std::max(std::abs(left), std::abs(right)));
        }
    }
    return peak;
}

} // namespace

int main()
{
    bool ok = true;

    s3g::DrumEcho echo;
    s3g::DrumEchoParams invalid;
    invalid.headMode = static_cast<s3g::DrumEchoHeadMode>(999u);
    invalid.clock = static_cast<s3g::DrumEchoClock>(999u);
    invalid.timeMs = -100.0f;
    invalid.feedback = 4.0f;
    invalid.wear = -2.0f;
    invalid.flutter = 8.0f;
    invalid.transient = -4.0f;
    invalid.sensitivity = 3.0f;
    invalid.duck = -1.0f;
    invalid.tone = 8.0f;
    invalid.spread = 5.0f;
    invalid.mix = -2.0f;
    invalid.outputGainDb = 90.0f;
    echo.setParams(invalid);
    const auto sanitized = echo.params();
    ok = ok
        && sanitized.headMode == s3g::DrumEchoHeadMode::Heads123
        && sanitized.clock == s3g::DrumEchoClock::Bar
        && sanitized.timeMs == 20.0f
        && sanitized.feedback == 0.92f
        && sanitized.wear == 0.0f
        && sanitized.flutter == 1.0f
        && sanitized.transient == -1.0f
        && sanitized.sensitivity == 1.0f
        && sanitized.duck == 0.0f
        && sanitized.tone == 1.0f
        && sanitized.spread == 1.0f
        && sanitized.mix == 0.0f
        && sanitized.outputGainDb == 12.0f;
    if (!ok) std::cerr << "Drum Echo parameter sanitation failed\n";

    s3g::DrumEchoParams bypassParams;
    bypassParams.bypass = true;
    echo.setParams(bypassParams);
    echo.prepare(48000.0);
    float bypassLeft = 0.321f;
    float bypassRight = -0.217f;
    echo.processFrame(bypassLeft, bypassRight);
    ok = ok && bypassLeft == 0.321f && bypassRight == -0.217f;
    if (bypassLeft != 0.321f || bypassRight != -0.217f) {
        std::cerr << "Drum Echo bypass was not sample exact\n";
    }

    s3g::DrumEchoParams headParams;
    headParams.clock = s3g::DrumEchoClock::Free;
    headParams.timeMs = 100.0f;
    headParams.feedback = 0.0f;
    headParams.wear = 0.0f;
    headParams.flutter = 0.0f;
    headParams.transient = 0.0f;
    headParams.duck = 0.0f;
    headParams.tone = 1.0f;
    headParams.spread = 0.0f;
    headParams.mix = 1.0f;
    headParams.outputGainDb = 0.0f;
    headParams.headMode = s3g::DrumEchoHeadMode::Heads123;
    const float head1 = renderEchoPeak(headParams, 0u, 98u, 108u);
    const float head2 = renderEchoPeak(headParams, 0u, 198u, 208u);
    const float head3 = renderEchoPeak(headParams, 0u, 298u, 308u);
    ok = ok && head1 > 0.08f && head2 > 0.08f && head3 > 0.08f;
    if (head1 <= 0.08f || head2 <= 0.08f || head3 <= 0.08f) {
        std::cerr << "Drum Echo multi-head timing failed: "
                  << head1 << ", " << head2 << ", " << head3 << "\n";
    }

    headParams.headMode = s3g::DrumEchoHeadMode::Head2;
    const float absentHead1 = renderEchoPeak(headParams, 0u, 98u, 108u);
    const float soloHead2 = renderEchoPeak(headParams, 0u, 198u, 208u);
    ok = ok && absentHead1 < 0.001f && soloHead2 > 0.08f;
    if (absentHead1 >= 0.001f || soloHead2 <= 0.08f) {
        std::cerr << "Drum Echo head selection failed\n";
    }

    headParams.headMode = s3g::DrumEchoHeadMode::Head1;
    headParams.clock = s3g::DrumEchoClock::Quarter;
    const float tempoEcho = renderEchoPeak(headParams, 0u, 498u, 508u);
    ok = ok && tempoEcho > 0.08f;
    if (tempoEcho <= 0.08f) {
        std::cerr << "Drum Echo tempo clock failed\n";
    }

    s3g::DrumEchoParams transientParams = headParams;
    transientParams.clock = s3g::DrumEchoClock::Free;
    transientParams.timeMs = 100.0f;
    transientParams.sensitivity = 1.0f;
    transientParams.transient = 1.0f;
    const float accented = renderEchoPeak(
        transientParams, 0u, 98u, 108u);
    transientParams.transient = -1.0f;
    const float suppressed = renderEchoPeak(
        transientParams, 0u, 98u, 108u);
    ok = ok && accented > suppressed * 2.0f;
    if (accented <= suppressed * 2.0f) {
        std::cerr << "Drum Echo transient injection failed: "
                  << accented << " vs " << suppressed << "\n";
    }

    s3g::DrumEchoParams duckParams = headParams;
    duckParams.clock = s3g::DrumEchoClock::Free;
    duckParams.timeMs = 100.0f;
    duckParams.sensitivity = 1.0f;
    duckParams.transient = 0.0f;
    duckParams.duck = 0.0f;
    const float unducked = renderEchoPeak(
        duckParams, 0u, 100u, 108u, true);
    duckParams.duck = 1.0f;
    const float ducked = renderEchoPeak(
        duckParams, 0u, 100u, 108u, true);
    ok = ok && ducked < unducked * 0.7f;
    if (ducked >= unducked * 0.7f) {
        std::cerr << "Drum Echo transient ducking failed: "
                  << ducked << " vs " << unducked << "\n";
    }

    s3g::DrumEchoParams stress;
    stress.headMode = s3g::DrumEchoHeadMode::Heads123;
    stress.clock = s3g::DrumEchoClock::Free;
    stress.timeMs = 24.0f;
    stress.feedback = 0.92f;
    stress.wear = 1.0f;
    stress.flutter = 1.0f;
    stress.transient = 1.0f;
    stress.sensitivity = 1.0f;
    stress.duck = 0.0f;
    stress.tone = 1.0f;
    stress.spread = 1.0f;
    stress.mix = 1.0f;
    stress.outputGainDb = 12.0f;
    echo.setParams(stress);
    echo.prepare(48000.0);
    float maximum = 0.0f;
    for (uint32_t frame = 0u; frame < 96000u; ++frame) {
        float left = frame % 997u == 0u ? 1.0f : 0.0f;
        float right = frame % 1481u == 0u ? -1.0f : 0.0f;
        echo.processFrame(left, right);
        maximum = std::max(maximum,
            std::max(std::abs(left), std::abs(right)));
        ok = ok && std::isfinite(left) && std::isfinite(right);
    }
    ok = ok && maximum <= 1.0001f && echo.tailSamples() > 0u;
    if (!std::isfinite(maximum) || maximum > 1.0001f) {
        std::cerr << "Drum Echo feedback safety failed: " << maximum << "\n";
    }

    if (!ok) return 1;
    std::cout << "s3g Drum Echo smoke passed\n";
    return 0;
}
