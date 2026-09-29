    unsigned familyPage = 0, familyDetail = 0, pathPoint = 0;
    int pathDragPoint = -1;
    bool pathPointLimit = false;
    std::size_t pathDragSlot = 0;
    std::atomic<float>& familyValue(NeonFamily key) { return p.familyControls[selectedSlot][neonFamilyIndex(key)]; }
    NeonFamilySettings familySnapshot() const {
        NeonFamilySettings result;
        for (unsigned i = 0; i < kNeonFamilyCount; ++i) result.values[i] = p.familyControls[selectedSlot][i].load();
        return result;
    }
    void storeStackPath(const NeonFamilySettings& f) {
        auto& values = p.familyControls[selectedSlot];
        for (auto key : {NeonFamily::StackShape, NeonFamily::PathCount, NeonFamily::StackCurve})
            values[neonFamilyIndex(key)].store(f[key]);
        for (unsigned i = neonFamilyIndex(NeonFamily::PathTime); i < kNeonFamilyV17Count; ++i) values[i].store(f.values[i]);
        const auto shape = static_cast<NeonStackShape>(f[NeonFamily::StackShape]);
        // Only a compatibility hint for v15 and earlier; DSP uses the points.
        if (shape != NeonStackShape::Manual) p.stackPaths[selectedSlot].store(
            shape == NeonStackShape::RampUp ? 0u : shape == NeonStackShape::RampDown ? 1u : shape == NeonStackShape::Wander ? 3u : 2u);
        markStateDirty(p); requestProcess(p);
    }
    void stackShapeMenu(unsigned row) {
        const auto choice = static_cast<unsigned>(familyValue(NeonFamily::StackShape).load());
        const std::vector<std::string> names(s3g::sample::kNeonStackShapeNames.begin(), s3g::sample::kNeonStackShapeNames.end());
        inspectorLabel("SHAPE", row);
        menu(inspectorMenu(row), names[choice], names, static_cast<int>(choice), [this](int n) {
            auto f = familySnapshot();
            neonSetStackShape(f, static_cast<NeonStackShape>(n), static_cast<unsigned>(f[NeonFamily::PathCount]), static_cast<unsigned>(selectedSlot));
            storeStackPath(f); pathPoint = std::min(pathPoint, static_cast<unsigned>(f[NeonFamily::PathCount]) - 1);
        });
    }
    template<class L = I> void familyMenu(NeonFamily key, const char* label, unsigned row, const std::vector<std::string>& names) {
        const auto choice = std::clamp(static_cast<int>(familyValue(key).load()), 0, static_cast<int>(names.size()) - 1);
        inspectorLabel<L>(label, row);
        menu(inspectorMenu<L>(row), names[choice], names, choice, [this, key](int n) {
            familyValue(key).store(static_cast<float>(n)); markStateDirty(p); requestProcess(p);
            if (key == NeonFamily::LaneAuto) takeStackNavigation(p, static_cast<unsigned>(selectedSlot));
        });
    }
    template<class L = I> void familySlider(NeonFamily key, const char* label, unsigned row, const char* unit = "") {
        const auto d = neonFamilyDef(neonFamilyIndex(key)); auto& target = familyValue(key);
        textureSliderRow(target, label, L::slider(row), d.minimum, d.maximum, d.initial,
            format(d.stepped ? "%.0f%s" : "%.2f%s", target.load(), unit), {},
            [stepped = d.stepped](double value) { return stepped ? std::round(value) : value; }, L::left, L::width,
            std::numeric_limits<double>::quiet_NaN(),d.stepped);
    }
    void paintFamilySettings() {
        using F = NeonFamily;
        if (familyPage == 3) {
            const bool pathActive = playbackChoice() == 6 ? familyValue(F::LaneAuto).load() != 0
                : p.sourceModes[selectedSlot].load() == 4;
            inspectorReadout(Labels::stackPath, stackCount(p, selectedSlot) < 2 ? "NEEDS 2+ LAYERS"
                : p.stackHeld[selectedSlot].load() ? "HELD PAD OVERRIDE"
                : p.stackManual[selectedSlot].load() >= 0 ? "MANUAL OVERRIDE"
                : !pathActive ? "NOT SELECTED"
                : p.slotPlaying[selectedSlot].load() && p.stackWaveformLayers[selectedSlot].load() == -1 ? "EDIT AUDITION"
                : !p.slotPlaying[selectedSlot].load() ? "READY / TRIGGER PAD"
                : p.playbackClocks[selectedSlot].load() && !p.transportPlaying.load() ? "HOST STOPPED" : "RUNNING", 4);
            stackShapeMenu(5);
            familyMenu(F::StackJump, "TRANSITION", 6, {"CROSSFADE", "JUMP"});
            if (playbackChoice() == 2) familyMenu(F::StackAdvance, "ADVANCE", 7, {"TIME", "GRAIN EVENTS"});
            else inspectorReadout("ADVANCE", "TIME", 7);
            familySlider(F::StackOffset, "PHASE", 8);
            familySlider(F::StackCurve, "CURVE", 9);
            clockMenu(I::menu(10));
            if (p.playbackClocks[selectedSlot].load()) beatMenu(p.stackBeats[selectedSlot], Labels::stackCycle, I::menu(11));
            else textureSliderRow(p.stackSeconds[selectedSlot], Labels::stackCycle, I::slider(11), .05f, 30, ControlDefaults::cycleSeconds,
                format("%.2f s", p.stackSeconds[selectedSlot].load()));
            const unsigned count = static_cast<unsigned>(familyValue(F::PathCount).load());
            const auto selectedShape = static_cast<NeonStackShape>(familyValue(F::StackShape).load());
            const bool manual = selectedShape == NeonStackShape::Manual;
            std::vector<std::string> counts, points;
            std::vector<bool> availableCounts;
            for (unsigned i = 2; i <= 32; ++i) {
                counts.push_back(format("%u", i)); availableCounts.push_back(i >= s3g::sample::neonStackShapeMinimum(selectedShape));
            }
            for (unsigned i = 0; i < count; ++i) points.push_back(format("%02u", i + 1));
            inspectorLabel("POINTS", 12);
            menu(inspectorMenu(12), format("%u", count), counts, static_cast<int>(count) - 2, [this](int n) {
                const auto old = familySnapshot(); const unsigned count = static_cast<unsigned>(n) + 2;
                if (old[NeonFamily::StackShape] != 0) {
                    auto f = old;
                    neonSetStackShape(f, static_cast<NeonStackShape>(f[NeonFamily::StackShape]), count, static_cast<unsigned>(selectedSlot));
                    storeStackPath(f); pathPoint = std::min(pathPoint, static_cast<unsigned>(f[NeonFamily::PathCount]) - 1);
                    return;
                }
                const auto t = neonFamilyIndex(NeonFamily::PathTime), v = neonFamilyIndex(NeonFamily::PathValue);
                auto shape = old; shape[NeonFamily::StackOffset] = 0; shape[NeonFamily::StackCurve] = 0;
                for (unsigned i = 0; i < count; ++i) {
                    const float phase = static_cast<float>(i) / (count - 1);
                    const float value = i + 1 == count ? old.values[v + static_cast<unsigned>(old[NeonFamily::PathCount]) - 1]
                        : static_cast<float>(s3g::sample::neonStackPath(shape, phase));
                    p.familyControls[selectedSlot][t + i].store(phase);
                    p.familyControls[selectedSlot][v + i].store(value);
                }
                familyValue(NeonFamily::PathCount).store(static_cast<float>(count));
                pathPoint = std::min(pathPoint, count - 1); markStateDirty(p); requestProcess(p);
            }, true, 2, kCenterText, availableCounts);
            pathPoint = std::min(pathPoint, count - 1);
            inspectorLabel("POINT", 13);
            menu(inspectorMenu(13), points[pathPoint], points, static_cast<int>(pathPoint), [this](int n) { pathPoint = n; }, true, 2);
            const auto t = neonFamilyIndex(F::PathTime), v = neonFamilyIndex(F::PathValue);
            auto& values = p.familyControls[selectedSlot];
            if (manual && pathPoint && pathPoint + 1 < count) {
                const float left = values[t + pathPoint - 1].load(), right = values[t + pathPoint + 1].load();
                const float margin = std::min(.0001f, (right - left) * .25f);
                const float lo = left + margin, hi = right - margin;
                textureSliderRow(values[t + pathPoint], "TIME", I::slider(14), lo, hi,
                    std::clamp(float(pathPoint) / (count - 1), lo, hi), format("%.2f%%", values[t + pathPoint].load() * 100));
            } else inspectorReadout("TIME", format("%.2f%%", values[t + pathPoint].load() * 100).c_str(), 14);
            const unsigned layers = stackCount(p, selectedSlot);
            const auto layerText = layers > 1 ? format("%.2f", 1 + values[v + pathPoint].load() * (layers - 1))
                : format("%.1f%%", values[v + pathPoint].load() * 100);
            if (manual && layers > 1) textureSliderRow(values[v + pathPoint], "POINT LAYER", I::slider(15), 0, 1, float(pathPoint) / (count - 1), layerText,
                [layers](double value) { return 1 + value * (layers - 1); }, [layers](double value) { return (value - 1) / (layers - 1); });
            else if (manual) textureSliderRow(values[v + pathPoint], "POINT LAYER", I::slider(15), 0, 1, float(pathPoint) / (count - 1), layerText);
            else inspectorReadout("POINT LAYER", layerText.c_str(), 15);
            button(inspectorButton(17), "RESET STACK PATH", [this] {
                auto f = familySnapshot(); const auto shape = static_cast<NeonStackShape>(f[NeonFamily::StackShape]);
                neonSetStackShape(f, shape == NeonStackShape::Manual ? NeonStackShape::RampUp : shape,
                    static_cast<unsigned>(f[NeonFamily::PathCount]), static_cast<unsigned>(selectedSlot));
                f[NeonFamily::StackShape] = static_cast<float>(shape);
                storeStackPath(f); pathPoint = 0;
            });
            return;
        }
    }

    void paintIntegratedFamily() {
        using F = NeonFamily;
        using FG = s3g::sample_neon_gui::FamilyGrid;
        const bool motion = playbackChoice() == 1;
        const unsigned sound = static_cast<unsigned>(familyValue(F::MotionSound).load());
        const auto extraMenu = [&](F key, const char* label, unsigned row, const std::vector<std::string>& choices) {
            familyMenu(key, label, FG::index(row, motion, sound), choices);
        };
        const auto extraSlider = [&](F key, const char* label, unsigned row, const char* unit = "") {
            familySlider(key, label, FG::index(row, motion, sound), unit);
        };
        if (motion) {
            extraMenu(F::MotionSound, "SOUND", 10, {"CONTINUOUS", "PACKETS", "MOTOR"});
            unsigned row = 11;
            if (sound == 1) {
                extraSlider(F::PacketRate, "PACKET RATE", row++, " Hz");
                extraSlider(F::PacketDuty, "DUTY", row++);
            } else if (sound == 2) {
                extraSlider(F::MotorRate, "MOTOR RATE", row++, " Hz");
                extraMenu(F::MotorShape, "MOTOR SHAPE", row++, {"LINEAR", "ROUNDED", "EXPONENTIAL", "PLATEAU"});
                extraSlider(F::MotorSymmetry, "SYMMETRY", row++);
            }
            extraSlider(F::MotionWindow, "JOIN WINDOW", row, " ms");
            familyMenu<E>(F::MotionTrajectory, "TRAJECTORY", 12, {Labels::sourcePath, "HOVER", "MIRROR", "ZIGZAG", "MOVING LOOP"});
            familySlider<E>(F::MotionLocus, "LOCUS", 13);
            familySlider<E>(F::MotionField, "FIELD", 14);
            familySlider<E>(F::MotionTravel, "TRAVEL", 15);
            familySlider<E>(F::MotionJitter, "JITTER", 16);
            familyMenu<E>(F::MotionModel, "EVENT MODEL", 17, {"SMOOTH", "FREEZE", "ITERATE", "DOUBLETS", "BOUNCE"});
            familySlider<E>(F::EventRate, "EVENT RATE", 18, " Hz");
            familySlider<E>(F::EventRepeats, "REPEATS", 19);
            familySlider<E>(F::EventStep, "STEP", 20);
            extraSlider(F::EventPitch, Labels::pitchSpray, 17, " st");
            extraSlider(F::EventLevel, "LEVEL VAR", 18);
            extraSlider(F::EventCurve, "ACCELERATE", 19);
        } else {
            familyMenu<E>(F::GrainSource, "GRAIN SRC", 15, {"FREEZE", "SCAN", "CLOUD", "SLICE"});
            familyMenu<E>(F::GrainBias, "SPRAY BIAS", 16, {"BEHIND", "AROUND", "AHEAD"});
            familySlider<E>(F::GrainRegions, "REGIONS", 17);
            familySlider<E>(F::GrainPitch, "GRAIN TUNE", 18, " st");
            if (p.playbackClocks[selectedSlot].load()) beatMenu(p.motionRates[selectedSlot], Labels::sourceCycle, E::menu(19));
            else textureSliderRow(p.motionSeconds[selectedSlot], Labels::sourceCycle, E::slider(19), .05f, 30, ControlDefaults::cycleSeconds,
                format("%.2f s", p.motionSeconds[selectedSlot].load()));
            familySlider<E>(F::GrainDensityScale, "DENSITY x", 20);
            const std::vector<std::string> paths {"FORWARD", "REVERSE", "BOUNCE", "WANDER"};
            const auto path = std::min<unsigned>(3, p.motionPaths[selectedSlot].load());
            inspectorLabel<E>(Labels::sourcePath, 21);
            menu(inspectorMenu<E>(21), paths[path], paths, path, [this](int n) {
                p.motionPaths[selectedSlot].store(static_cast<uint8_t>(n)); markStateDirty(p); requestProcess(p);
            });
            extraMenu(F::GrainWindow, "ENVELOPE", 10, {"LEGACY", "PARZEN", "SINE", "HANN", "TRIANGLE", "GAUSSIAN"});
            extraSlider(F::GrainSkew, "SKEW", 11);
            extraSlider(F::GrainSizeScale, "SIZE x", 12);
            extraSlider(F::GrainSizeVariation, "SIZE VAR", 13);
            extraSlider(F::GrainLevelVariation, "LEVEL VAR", 14);
            extraSlider(F::GrainScatter, "TIME SCATTER", 15);
            extraMenu(F::GrainProcess, "PROCESS", 17, {"ORDINARY", "SORTER", "STUTTER", "SHRINK", "DOUBLETS"});
            extraSlider(F::GrainAmount, "AMOUNT", 18);
            extraMenu(F::GrainTimeSync, "DOUBLETS", 19, {"SAME POSITION", "SOURCE TIME"});
        }
        sliderRow(slotParamId(selectedSlot, kSlotFilterCutoff), "CUTOFF", I::slider(FG::cutoff(motion, sound)));
    }
    void editStackPathAt(CPoint pos, bool remove) {
        auto f = familySnapshot(); const auto area = stackPathRect();
        const auto t = neonFamilyIndex(NeonFamily::PathTime), v = neonFamilyIndex(NeonFamily::PathValue);
        const unsigned count = static_cast<unsigned>(f[NeonFamily::PathCount]);
        int nearest = -1; double distance = 12 * 12;
        for (unsigned n = 0; n < count; ++n) {
            const double dx = pos.x - (area.left + area.getWidth() * f.values[t + n]);
            const double dy = pos.y - (area.top + area.getHeight() * f.values[v + n]);
            if (dx * dx + dy * dy <= distance) { distance = dx * dx + dy * dy; nearest = static_cast<int>(n); }
        }
        if (remove) {
            if (nearest >= 0 && s3g::sample::neonRemoveStackPoint(f, static_cast<unsigned>(nearest))) {
                pathPoint = std::min(static_cast<unsigned>(nearest), static_cast<unsigned>(f[NeonFamily::PathCount]) - 1);
                storeStackPath(f);
            }
            return;
        }
        f[NeonFamily::StackShape] = static_cast<float>(NeonStackShape::Manual);
        if (nearest < 0) nearest = s3g::sample::neonAddStackPoint(f,
            static_cast<float>((pos.x - area.left) / area.getWidth()),
            static_cast<float>((pos.y - area.top) / area.getHeight()), static_cast<float>(10 / area.getWidth()));
        if (nearest >= 0) pathPoint = static_cast<unsigned>(nearest);
        pathPointLimit = nearest < 0 && count == 32;
        pathDragPoint = nearest; pathDragSlot = selectedSlot;
        storeStackPath(f);
    }
    void paintStackPathGraph() {
        const auto area = stackPathRect();
        fill(area, style.strip); stroke(area, style.dim);
        auto f = familySnapshot(); const auto t = neonFamilyIndex(NeonFamily::PathTime), v = neonFamilyIndex(NeonFamily::PathValue);
        const unsigned count = static_cast<unsigned>(f[NeonFamily::PathCount]);
        const unsigned layers = stackCount(p, selectedSlot);
        // Match Stack Lanes: normalized zero (layer 1) is at the top. Only
        // the screen mapping changes; authored values and playback stay intact.
        for (unsigned i = 1; i + 1 < layers; ++i) {
            const double y = area.top + area.getHeight() * i / (layers - 1);
            auto shade = style.dim; shade.alpha = 60;
            line(CPoint(area.left, y), CPoint(area.right, y), shade);
        }
        auto point = [&](unsigned n) { return CPoint(area.left + area.getWidth() * f.values[t + n], area.top + area.getHeight() * f.values[v + n]); };
        // Draw the actual curved path, independent of the phase offset.
        f[NeonFamily::StackOffset] = 0;
        CPoint previous = point(0);
        for (unsigned x = 1; x <= W::pathWidth; ++x) {
            const double value = x == W::pathWidth ? f.values[v + count - 1] : s3g::sample::neonStackPath(f, x / W::pathWidth);
            CPoint next(area.left + x, area.top + value * area.getHeight());
            line(previous, next, style.value); previous = next;
        }
        for (unsigned n = 0; n < count; ++n) {
            const auto at = point(n);
            fill(rect(at.x - 2, at.y - 2, 4, 4), n == pathPoint ? style.text : style.value);
        }
        const float phase = p.stackPathPhases[selectedSlot].load();
        if (phase >= 0 && phase <= 1 && layers > 1) {
            const double x = area.left + phase * area.getWidth();
            const double y = area.top + s3g::sample::neonStackPath(f, phase) * area.getHeight();
            const bool overridden = p.stackTargets[selectedSlot].load() >= 0;
            line({x, area.top}, {x, area.bottom}, style.dim);
            ellipse(rect(x - 4, y - 4, 8, 8), overridden ? style.value : color(0x91aaa3), !overridden, 1.5);
        }
    }
