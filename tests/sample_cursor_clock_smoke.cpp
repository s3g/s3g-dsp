#include "../plugins/common/s3g_sample_cursor_clock.h"
#include <cmath>
#include <iostream>

int main()
{
    using namespace s3g::portable_gui;
    bool ok = true;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; ok = false; }
    };
    const auto near = [](double a, double b) { return std::abs(a - b) < 1e-8; };
    SampleCursorClock clock;
    SampleCursorTrajectory t;
    t.asset = 1; t.identity = 1; t.position = .2; t.low = .1; t.high = .9;
    t.rate = .25; t.running = true; t.loop = true;
    expect(clock.synchronize(t, 0), "initial contract must install");
    expect(near(clock.positionAt(2), .7), "frozen DSP publication must keep moving");
    t.position = .85;
    expect(!clock.synchronize(t, 2), "routine/anticipative positions must not restart animation");
    expect(near(clock.positionAt(4), .4), "loop must wrap without UI ticks");
    t.rate = .5;
    expect(clock.synchronize(t, 4) && near(clock.positionAt(4), .4),
        "rate edit must preserve analytic phase, not stale presentationLayer");
    t.running = false;
    clock.synchronize(t, 4.2);
    expect(near(clock.positionAt(40), .5), "pause must retain the presentation phase");
    t.discontinuity++; t.position = .3;
    clock.synchronize(t, 41);
    expect(near(clock.positionAt(41), .3), "cue/restart discontinuity must snap");
    t.discontinuity++; t.running = true; t.rate = -.5; t.loop = false;
    clock.synchronize(t, 42);
    expect(near(clock.positionAt(50), .1), "reverse one-shot must stop at lower bound");
    t.discontinuity++; t.position = .2; t.rate = .25; t.loop = true; t.pingPong = true;
    clock.synchronize(t, 60);
    expect(near(clock.positionAt(64), .6) && near(clock.rateAt(64), -.25),
        "ping-pong must reflect independently");
    t.rate = .5;
    clock.synchronize(t, 64);
    expect(near(clock.positionAt(64.2), .5), "ping-pong rate edit must preserve descending leg");
    expect(!clock.synchronize(t, 64.2), "unchanged ping-pong contract must not reinstall");
    t = {}; t.asset = 2; t.identity = 8; t.position = .1; t.smoothObserved = true;
    clock.synchronize(t, 70);
    t.position = .9;
    clock.synchronize(t, 71);
    expect(near(clock.positionAt(71 + 1.0 / 60), .5)
        && near(clock.positionAt(72), .9), "Motion must preserve its original 30 Hz interpolation");
    t.identity++; t.position = .4;
    clock.synchronize(t, 72);
    expect(near(clock.positionAt(72), .4), "new voice must not interpolate from a stolen voice");
    return ok ? 0 : 1;
}
