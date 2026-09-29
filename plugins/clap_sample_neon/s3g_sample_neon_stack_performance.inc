// Audio-thread gesture ownership; GUI sends bounded atomic commands only.
// No file publication, allocation, edit-layer changes or saved path mutation.
bool stackPlayable(const Plugin& p, unsigned slot) {
    const auto method = playbackIndex(p, slot);
    return method != 3u && method != 8u && (method != 5u || p.sourceModes[slot].load() == 4u);
}
void queueStackManual(Plugin& p, unsigned slot, double position) {
    p.pendingStackManual[slot].store(1u + static_cast<uint32_t>(std::lround(std::clamp(position, 0.0, 1.0) * 1000000)));
    requestProcess(p);
}
void resetStackPerformance(Plugin& p, bool clearCommands = true) {
    p.stackGestures = {}; p.stackGestureOrder = 0;
    for (unsigned slot = 0; slot < 32; ++slot) {
        p.stackManual[slot].store(-1); p.stackTargets[slot].store(-1);
        p.stackHeld[slot].store(false);
        if (clearCommands) p.pendingStackManual[slot].store(0);
    }
    if (clearCommands) {
        for (auto& command : p.pendingStackGui) command.store(0);
        p.pendingStackGuiReleases.store(0); p.pendingStackResume.store(0);
        p.pendingStackNavigation.store(0);
        p.clearStackPerformance.store(false);
    }
}
void stackEvent(Plugin& p, std::size_t& count, unsigned slot, uint32_t time,
    SampleNeonEventKind kind, float value, unsigned gesture = 0, unsigned layer = 255) {
    if (count >= p.blockEvents.size() || slot >= 32) return;
    auto& event = p.blockEvents[count++]; event = {};
    event.frameOffset = time; event.slot = static_cast<uint8_t>(slot); event.kind = kind;
    event.value = value; event.noteId = 0x78000000u + gesture;
    event.sourceLayer = static_cast<uint8_t>(layer);
}
void publishStackTarget(Plugin& p, unsigned slot, uint32_t time, std::size_t& count, bool navigation = false) {
    const auto* stack = p.publishedStacks[slot].load();
    const Plugin::StackGesture* last = nullptr;
    for (const auto& g : p.stackGestures)
        if (g.active && g.slot == slot && stack && g.layer < stack->count && (!last || g.order > last->order)) last = &g;
    const float target = last ? stack->count > 1 ? float(last->layer) / (stack->count - 1) : 0 : p.stackManual[slot].load();
    p.stackTargets[slot].store(target);
    p.stackHeld[slot].store(last != nullptr);
    const auto before = count;
    stackEvent(p, count, slot, time, SampleNeonEventKind::StackPosition, target);
    if (count != before) p.blockEvents[count - 1u].resumeNavigation = navigation;
    p.ledFeedbackDirty.store(true);
}
void stackPerform(Plugin& p, unsigned gesture, unsigned slot, unsigned layer, bool down,
    uint32_t time, std::size_t& count, float velocity = 1) {
    auto& g = p.stackGestures[gesture];
    if (g.active) {
        const auto old = g; g = {};
        if (old.triggered) stackEvent(p, count, old.slot, time, SampleNeonEventKind::Release, 0, gesture);
        publishStackTarget(p, old.slot, time, count);
    }
    if (!down || slot >= 32 || !stackPlayable(p, slot)) return;
    const auto* stack = p.publishedStacks[slot].load();
    if (!stack || layer >= stack->count || !stack->layers[layer].asset) return;
    // Sample always auditions a layer; a running generated voice is steered
    // in place, without retriggering its envelope or resetting its clock.
    bool startedInBlock = false;
    for (const auto& held : p.stackGestures) startedInBlock |= held.active && held.slot == slot && held.triggered;
    const bool start = playbackIndex(p, slot) == 0 || (!p.slotPlaying[slot].load() && !startedInBlock);
    g = {true, start, static_cast<uint8_t>(slot), static_cast<uint8_t>(layer), ++p.stackGestureOrder};
    publishStackTarget(p, slot, time, count);
    if (start) stackEvent(p, count, slot, time, SampleNeonEventKind::Trigger, velocity, gesture, layer);
}
void serviceStackPerformance(Plugin& p, std::size_t& count) {
    if (p.clearStackPerformance.exchange(false)) {
        for (unsigned n = 0; n < 24; ++n) stackPerform(p, n, 0, 0, false, 0, count);
        for (unsigned slot = 0; slot < 32; ++slot) {
            if (p.stackManual[slot].load() >= 0) stackEvent(p,count,slot,0,SampleNeonEventKind::Release,0,32+slot);
            p.stackManual[slot].store(-1); publishStackTarget(p, slot, 0, count);
        }
        resetStackPerformance(p, false); // Keep fresh GUI commands submitted after stop/restore.
    }
    const auto resume = p.pendingStackResume.exchange(0);
    const auto navigation = p.pendingStackNavigation.exchange(0);
    for (unsigned slot = 0; slot < 32; ++slot) {
        const auto manual = p.pendingStackManual[slot].exchange(0);
        const float previous = p.stackManual[slot].load();
        if ((resume | navigation) & (1u << slot)) p.stackManual[slot].store(-1);
        else if (manual && stackPlayable(p, slot)) p.stackManual[slot].store((manual - 1) / 1000000.f);
        if (playbackIndex(p, slot) == 0) {
            const auto* stack = p.publishedStacks[slot].load();
            const float position = p.stackManual[slot].load();
            const auto nearest = [&](float at) {
                unsigned result = 32; float distance = 33;
                if (stack) for (unsigned layer=0;layer<stack->count;++layer) if (stack->layers[layer].asset) {
                    const float d = std::abs(at * (stack->count - 1) - layer);
                    if (d < distance) {distance=d;result=layer;}
                }
                return result;
            };
            const unsigned layer=nearest(position);
            if (position < 0 && previous >= 0) stackEvent(p,count,slot,0,SampleNeonEventKind::Release,0,32+slot);
            else if (manual && layer < 32 && (previous < 0 || nearest(previous)!=layer))
                stackEvent(p,count,slot,0,SampleNeonEventKind::Trigger,1,32+slot,layer);
        }
        if (manual || ((resume | navigation) & (1u << slot)))
            publishStackTarget(p, slot, 0, count, (navigation & (1u << slot)) != 0);
    }
    const auto released = p.pendingStackGuiReleases.exchange(0);
    for (unsigned n = 0; n < 8; ++n) {
        const auto press = p.pendingStackGui[n].exchange(0);
        if (press) stackPerform(p, 8+n, (press - 1) >> 5u, (press - 1) & 31u, true, 0, count);
        if (released & (1u << n)) stackPerform(p, 8+n, 0, 0, false, 0, count);
    }
}
