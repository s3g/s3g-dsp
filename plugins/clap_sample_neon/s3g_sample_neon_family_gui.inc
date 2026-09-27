    unsigned familyPage = 0, familyDetail = 0, pathPoint = 0;
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
    void familyMenu(NeonFamily key, const char* label, unsigned row, const std::vector<std::string>& names) {
        const auto choice = std::clamp(static_cast<int>(familyValue(key).load()), 0, static_cast<int>(names.size()) - 1);
        inspectorLabel(label, row);
        menu(inspectorMenu(row), names[choice], names, choice, [this, key](int n) {
            familyValue(key).store(static_cast<float>(n)); markStateDirty(p); requestProcess(p);
        });
    }
    void familySlider(NeonFamily key, const char* label, unsigned row, const char* unit = "") {
        const auto d = neonFamilyDef(neonFamilyIndex(key)); auto& target = familyValue(key);
        textureSliderRow(target, label, I::slider(row), d.minimum, d.maximum, d.initial,
            format(d.stepped ? "%.0f%s" : "%.2f%s", target.load(), unit), {},
            [stepped = d.stepped](double value) { return stepped ? std::round(value) : value; });
    }
    void paintFamilySettings() {
        using F = NeonFamily;
        if (familyPage == 3) {
            sourceModeMenu(4);
            stackShapeMenu(5);
            familyMenu(F::StackJump, "TRANSITION", 6, {"CROSSFADE", "JUMP"});
            if (playbackChoice() == 2) familyMenu(F::StackAdvance, "ADVANCE", 7, {"TIME", "GRAIN EVENTS"});
            else inspectorReadout("ADVANCE", "TIME", 7);
            familySlider(F::StackOffset, "PHASE", 8);
            familySlider(F::StackCurve, "CURVE", 9);
            clockMenu(I::menu(10));
            if (p.playbackClocks[selectedSlot].load()) beatMenu(p.stackBeats[selectedSlot], "STACK CYCLE", I::menu(11));
            else textureSliderRow(p.stackSeconds[selectedSlot], "STACK CYCLE", I::slider(11), .05f, 30, ControlDefaults::cycleSeconds,
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
            if (manual && layers > 1) textureSliderRow(values[v + pathPoint], "LAYER POS", I::slider(15), 0, 1, float(pathPoint) / (count - 1), layerText,
                [layers](double value) { return 1 + value * (layers - 1); }, [layers](double value) { return (value - 1) / (layers - 1); });
            else if (manual) textureSliderRow(values[v + pathPoint], "LAYER POS", I::slider(15), 0, 1, float(pathPoint) / (count - 1), layerText);
            else inspectorReadout("LAYER POS", layerText.c_str(), 15);
            button(inspectorButton(17), "RESET PATH", [this] {
                auto f = familySnapshot(); const auto shape = static_cast<NeonStackShape>(f[NeonFamily::StackShape]);
                neonSetStackShape(f, shape == NeonStackShape::Manual ? NeonStackShape::RampUp : shape,
                    static_cast<unsigned>(f[NeonFamily::PathCount]), static_cast<unsigned>(selectedSlot));
                f[NeonFamily::StackShape] = static_cast<float>(shape);
                storeStackPath(f); pathPoint = 0;
            });
            return;
        }
        const bool motion = familyPage == 1;
        const std::vector<std::string> sections = motion ? std::vector<std::string>{"ARTICULATION", "MOVEMENT", "EVENTS"}
            : std::vector<std::string>{"SOURCE", "WINDOW", "PROCESS"};
        familyDetail = std::min(2u, familyDetail);
        inspectorLabel("DETAIL", 4);
        menu(inspectorMenu(4), sections[familyDetail], sections, static_cast<int>(familyDetail), [this](int n) { familyDetail = n; });
        if (motion && familyDetail == 0) {
            familyMenu(F::MotionSound, "SOUND", 5, {"CONTINUOUS", "PACKETS", "MOTOR"});
            familySlider(F::PacketRate, "PACKET RATE", 6, " Hz");
            familySlider(F::PacketDuty, "DUTY", 7);
            familySlider(F::MotorRate, "MOTOR RATE", 8, " Hz");
            familyMenu(F::MotorShape, "MOTOR SHAPE", 9, {"LINEAR", "ROUNDED", "EXPONENTIAL", "PLATEAU"});
            familySlider(F::MotorSymmetry, "SYMMETRY", 10);
            familySlider(F::MotionWindow, "JOIN WINDOW", 11, " ms");
        } else if (motion && familyDetail == 1) {
            familyMenu(F::MotionTrajectory, "TRAJECTORY", 5, {"BASIC PATH", "HOVER", "MIRROR", "ZIGZAG", "MOVING LOOP"});
            familySlider(F::MotionLocus, "LOCUS", 6);
            familySlider(F::MotionField, "FIELD", 7);
            familySlider(F::MotionTravel, "TRAVEL", 8);
            familySlider(F::MotionJitter, "JITTER", 9);
        } else if (motion) {
            familyMenu(F::MotionModel, "EVENT MODEL", 5, {"SMOOTH", "FREEZE", "ITERATE", "DOUBLETS", "BOUNCE"});
            familySlider(F::EventRate, "EVENT RATE", 6, " Hz");
            familySlider(F::EventRepeats, "REPEATS", 7);
            familySlider(F::EventStep, "STEP", 8);
            familySlider(F::EventPitch, "PITCH SPRAY", 9, " st");
            familySlider(F::EventLevel, "LEVEL VAR", 10);
            familySlider(F::EventCurve, "ACCELERATE", 11);
        } else if (familyDetail == 0) {
            familyMenu(F::GrainSource, "SOURCE", 5, {"FREEZE", "SCAN", "CLOUD", "SLICE"});
            familyMenu(F::GrainBias, "SPRAY BIAS", 6, {"BEHIND", "AROUND", "AHEAD"});
            familySlider(F::GrainRegions, "REGIONS", 7);
            familySlider(F::GrainPitch, "GRAIN TUNE", 8, " st");
            clockMenu(I::menu(9));
            if (p.playbackClocks[selectedSlot].load()) beatMenu(p.motionRates[selectedSlot], "SCAN CYCLE", I::menu(10));
            else textureSliderRow(p.motionSeconds[selectedSlot], "SCAN CYCLE", I::slider(10), .05f, 30, ControlDefaults::cycleSeconds,
                format("%.2f s", p.motionSeconds[selectedSlot].load()));
            familySlider(F::GrainDensityScale, "DENSITY x", 11);
            const std::vector<std::string> paths {"FORWARD", "REVERSE", "BOUNCE", "WANDER"};
            const auto path = std::min<unsigned>(3, p.motionPaths[selectedSlot].load());
            inspectorLabel("SCAN PATH", 12);
            menu(inspectorMenu(12), paths[path], paths, static_cast<int>(path), [this](int n) {
                p.motionPaths[selectedSlot].store(static_cast<uint8_t>(n)); markStateDirty(p); requestProcess(p);
            });
        } else if (familyDetail == 1) {
            familyMenu(F::GrainWindow, "ENVELOPE", 5, {"LEGACY", "PARZEN", "SINE", "HANN", "TRIANGLE", "GAUSSIAN"});
            familySlider(F::GrainSkew, "SKEW", 6);
            familySlider(F::GrainSizeScale, "SIZE x", 7);
            familySlider(F::GrainSizeVariation, "SIZE VAR", 8);
            familySlider(F::GrainLevelVariation, "LEVEL VAR", 9);
            familySlider(F::GrainScatter, "TIME SCATTER", 10);
        } else {
            familyMenu(F::GrainProcess, "PROCESS", 5, {"ORDINARY", "SORTER", "STUTTER", "SHRINK", "DOUBLETS"});
            familySlider(F::GrainAmount, "AMOUNT", 6);
            familySlider(F::GrainRegions, "REGIONS", 7);
            familyMenu(F::GrainTimeSync, "DOUBLETS", 8, {"SAME POSITION", "SOURCE TIME"});
        }
    }
    void paintStackPathGraph() {
        const auto area = rect(177, 426, 635, 72);
        fill(area, style.strip); stroke(area, style.dim);
        auto f = familySnapshot(); const auto t = neonFamilyIndex(NeonFamily::PathTime), v = neonFamilyIndex(NeonFamily::PathValue);
        const unsigned count = static_cast<unsigned>(f[NeonFamily::PathCount]);
        const unsigned layers = stackCount(p, selectedSlot);
        for (unsigned i = 1; i + 1 < layers; ++i) {
            const double y = area.bottom - area.getHeight() * i / (layers - 1);
            auto shade = style.dim; shade.alpha = 60;
            line(CPoint(area.left, y), CPoint(area.right, y), shade);
        }
        auto point = [&](unsigned n) { return CPoint(area.left + area.getWidth() * f.values[t + n], area.bottom - area.getHeight() * f.values[v + n]); };
        // Draw the actual curved path, independent of the phase offset.
        f[NeonFamily::StackOffset] = 0;
        CPoint previous = point(0);
        for (unsigned x = 1; x <= 635; ++x) {
            const double value = x == 635 ? f.values[v + count - 1] : s3g::sample::neonStackPath(f, x / 635.0);
            CPoint next(area.left + x, area.bottom - value * area.getHeight());
            line(previous, next, style.value); previous = next;
        }
        for (unsigned n = 0; n < count; ++n) {
            const auto at = point(n); const auto node = rect(at.x - 5, at.y - 5, 10, 10);
            fill(rect(at.x - 2, at.y - 2, 4, 4), n == pathPoint ? style.text : style.value);
            const auto slot = selectedSlot;
            if (f[NeonFamily::StackShape] != 0) {
                hit(node, [this, n] { pathPoint = n; });
                continue;
            }
            drag(node, [this, area, slot, n, count, t, v](CPoint pos) {
                auto& controls = p.familyControls[slot];
                if (n && n + 1 < count) {
                    const double left = controls[t + n - 1].load(), right = controls[t + n + 1].load();
                    const double margin = std::min(.0001, (right - left) * .25);
                    controls[t + n].store(static_cast<float>(std::clamp((pos.x - area.left) / area.getWidth(), left + margin, right - margin)));
                }
                controls[v + n].store(static_cast<float>(std::clamp((area.bottom - pos.y) / area.getHeight(), 0.0, 1.0)));
                markStateDirty(p); requestProcess(p);
            }, [this, n] { pathPoint = n; }, {}, [this, slot, n, count, v] {
                p.familyControls[slot][v + n].store(float(n) / (count - 1)); markStateDirty(p); requestProcess(p);
            });
        }
        text("STACK PATH", rect(32, 480, 140, 18), style.value);
    }
