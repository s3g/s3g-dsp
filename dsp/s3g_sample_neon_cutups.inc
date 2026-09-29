    // Reuse Cutups' actual voice/cut scheduler, readers, random stream,
    // joins and allocator. Neon supplies a pad's entire immutable layer stack.
    void renderCutups(const SampleNeonSettings& settings, std::size_t slot,
        const SampleNeonEvent* input, std::size_t count, float* const* channels, uint32_t frames) noexcept
    {
        using F = NeonFamily;
        const auto& c = settings.slots[slot]; const auto& f = c.family;
        auto& engine = cutups_[slot];
        BasicSampleCutupsSettings<kSampleNeonLayers> s;
        s.clockBasis = c.clock == SampleNeonClock::Host ? CutClockBasis::Host : CutClockBasis::Hertz;
        s.division = static_cast<CutDivision>(f[F::CutDivision]); s.cutRateHz = f[F::CutRate];
        s.regionMode = static_cast<CutRegionMode>(f[F::CutRegionMode]);
        s.regionCount = s.patternLength = static_cast<unsigned>(f[F::CutRegions]);
        s.repeatCount = static_cast<unsigned>(f[F::CutRepeat]);
        s.fileOrder = static_cast<CutFileOrder>(f[F::CutFileOrder]);
        s.sourceOrder = static_cast<CutSourceOrder>(f[F::CutSourceOrder]);
        s.swing = f[F::CutSwing]; s.timeVariation = f[F::CutTimeVariation];
        s.gate = f[F::CutGate]; s.joinMilliseconds = f[F::CutJoin];
        s.reverseChance = f[F::CutReverse]; s.pitchVariationSemitones = f[F::CutPitchVariation];
        s.levelVariation = f[F::CutLevelVariation]; s.tempoSync = f[F::CutTempoSync] != 0;
        s.seed = static_cast<unsigned>(f[F::CutSeed]); s.voiceMode = static_cast<VoiceMode>(f[F::CutVoiceMode]);
        s.polyPathMode = static_cast<CutPolyPathMode>(f[F::CutPolyPath]);
        s.allocationCadence = static_cast<CutAllocationCadence>(f[F::CutAllocation]);
        s.attackSeconds = f[F::CutAttack]; s.releaseSeconds = f[F::CutRelease];
        s.triggerMode = c.triggerMode; s.tuneSemitones = c.tuneSemitones;
        s.outputGainDecibels = c.gainDecibels; s.velocitySensitivity = c.velocityEnabled ? 1.f : 0.f;
        s.outputMode = neonDistributes(c) ? CutOutputMode::Distribute : CutOutputMode::Preserve;
        s.activeOutputChannels = sampleNeonBusWidth(settings.outputLayout);
        s.outputRouting.width = static_cast<s3g::routing::OutputVoiceWidth>(f[F::RoutingWidth]);
        s.outputRouting.traversal = static_cast<s3g::routing::OutputTraversal>(f[F::RoutingTraversal]);
        s.useLaneWindows = true;
        for (unsigned n = 0; n < kMaximumCutupsPatternSteps; ++n) {
            s.manualPattern[n] = {static_cast<uint8_t>(f.values[neonFamilyIndex(F::CutPatternLane)+n]),
                f.values[neonFamilyIndex(F::CutPatternSource)+n]};
        }
        for (unsigned n = 0; n < kSampleNeonLayers; ++n) {
            const bool inStack = c.stack && n < c.stack->count;
            const auto layer = inStack ? stackLayer(c,n,assets_[slot])
                : n == 0 && (!c.stack || !c.stack->count) ? NeonStackLayer {assets_[slot],c.start,c.end} : NeonStackLayer {};
            engine.setPreparedAsset(n,layer.asset);
            engine.setPreparedMetadata(n,inStack ? &c.stack->cutupsMetadata[n] : nullptr);
            s.laneStarts[n] = layer.start; s.laneEnds[n] = layer.end;
            s.laneBpm[n] = inStack && c.stack->sourceBpms[n] > 0 ? std::clamp(c.stack->sourceBpms[n],20.f,400.f) : 120.f;
            if (n == c.selectedLayer) s.laneBpm[n] = static_cast<float>(std::clamp(c.sourceTempoBpm,20.,400.));
        }
        // Like Wavesets, direct CHOP/source audition owns only its portion of
        // the block; never overwrite audio on the other side of a transition.
        std::array<CutupsRenderEvent,kSampleNeonMaximumBlockEvents> events {};
        std::size_t total = 0; uint32_t begin = 0;
        const auto renderTo = [&](uint32_t end) {
            if (waveActive_[slot] && end > begin) {
                std::array<float*,16> segment {};
                for (unsigned ch = 0; ch < segment.size(); ++ch) segment[ch] = channels[ch]+begin;
                engine.render(s,events.data(),total,segment.data(),16,end-begin,settings.hostTempoBpm);
            }
            begin = end; total = 0;
        };
        for (std::size_t n = 0; n < count; ++n) {
            const auto& event = input[n];
            if (event.slot >= kSampleNeonSlotCount || event.kind == SampleNeonEventKind::Pressure
                || event.kind == SampleNeonEventKind::StackPosition) continue;
            const auto at = std::clamp(event.frameOffset,begin,frames);
            if (event.slot != slot) {
                if (waveActive_[slot] && c.chokeGroup && settings.slots[event.slot].chokeGroup == c.chokeGroup
                    && event.kind == SampleNeonEventKind::Trigger)
                    events[total++] = {at-begin,CutupsEventKind::StopAll};
                continue;
            }
            if (event.kind == SampleNeonEventKind::Trigger) {
                const bool next = event.mode == controller::reloop_neon::Mode::Sampler;
                if (waveActive_[slot] != next) { renderTo(at); engine.reset(); waveActive_[slot] = next; }
            }
            if (waveActive_[slot]) events[total++] = {at-begin,
                event.kind == SampleNeonEventKind::Trigger ? CutupsEventKind::NoteOn
                    : event.kind == SampleNeonEventKind::Release ? CutupsEventKind::NoteOff : CutupsEventKind::StopAll,
                c.triggerMode == TriggerMode::Toggle ? 0u : event.noteId, 60u, event.value, 0u};
        }
        renderTo(frames);
        waveCursorCounts_[slot] = waveActive_[slot] ? engine.voiceCursorCount() : 0;
        for (unsigned n = 0; n < waveCursorCounts_[slot]; ++n) {
            const auto& cut = engine.voiceCursors()[n];
            const auto layer = stackLayer(c,cut.lane,assets_[slot]);
            auto& cursor = waveCursors_[slot][n];
            cursor = {cut.sourcePositionNormalized,cut.key,static_cast<float>(layer.start),
                static_cast<float>(layer.end),cut.identity,layer.asset};
            cursor.layer = cut.lane;
            cursor.reverse = cut.reverse; cursor.level = cut.level;
            cursor.windowPhase = cut.currentPatternStep / static_cast<float>(s.patternLength);
            stackPositions_[slot] = cut.lane / static_cast<float>(std::max(1u,c.stack && c.stack->count ? unsigned(c.stack->count)-1 : 0u));
            stackPathPhases_[slot] = cursor.windowPhase;
        }
    }
