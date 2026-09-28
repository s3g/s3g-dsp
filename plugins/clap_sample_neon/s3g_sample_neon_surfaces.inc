// Per-USB-unit editing context. The existing atomics are the visible editor's
// context. Only real controls change focus; background sync/LED updates don't.
void saveSurface(Plugin& p) noexcept {
    auto& s = p.surfaces[p.surfaceUnit];
    s.bank = p.visibleBank.load(); s.mode = p.visibleMode.load(); s.layer = p.visibleLayer.load();
    s.slice = p.visibleSlice.load(); s.selected = p.selectedSlot.load();
    s.cellBank = p.cellBank.load(); s.chopBank = p.chopBank.load(); s.stackBank = p.stackBank.load();
    s.editPage = p.editPage.load(); s.editEncoders = p.playEditEncoders.load();
    s.state = p.neonState; s.decoder = p.neonPadInput;
    p.surfaceFeedbackDirty[p.surfaceUnit] |= p.ledFeedbackDirty.exchange(false);
}
void focusSurface(Plugin& p, unsigned unit) noexcept {
    if (unit == p.surfaceUnit) return;
    saveSurface(p);
    const auto& s = p.surfaces[unit];
    p.visibleBank.store(s.bank); p.visibleMode.store(s.mode); p.visibleLayer.store(s.layer);
    p.visibleSlice.store(s.slice); p.selectedSlot.store(s.selected);
    p.cellBank.store(s.cellBank); p.chopBank.store(s.chopBank); p.stackBank.store(s.stackBank);
    p.editPage.store(s.editPage); p.playEditEncoders.store(s.editEncoders);
    p.neonState = s.state; p.neonPadInput = s.decoder;
    p.surfaceUnit = unit; p.visibleUnit.store(unit);
}
struct RestoreSurfaceFocus {
    Plugin& plugin;
    unsigned previous;
    bool temporary = false;
    ~RestoreSurfaceFocus() { if (temporary) focusSurface(plugin, previous); }
};
void releaseSurface(Plugin& p, unsigned unit, uint32_t time, std::size_t& count) noexcept {
    for (unsigned pad = 0; pad < 8; ++pad) {
        auto& held = p.heldGestures[unit*8u+pad];
        if (held.active && count < p.blockEvents.size()) {
            auto event = held.event; event.frameOffset = time;
            event.kind = held.pressureOnly ? SampleNeonEventKind::Pressure : SampleNeonEventKind::Release;
            event.value = 0; p.blockEvents[count++] = event;
        }
        held = {};
        stackPerform(p, unit ? 16u+pad : pad, 0, 0, false, time, count);
    }
    p.surfaceFill[unit] = false;
    p.fillHardwareHeld.store(p.surfaceFill[0] || p.surfaceFill[1]); fillEvent(p, time);
    auto& s = p.surfaces[unit]; s.state = {}; s.decoder.clear();
    if (unit == p.surfaceUnit) { p.neonState = {}; p.neonPadInput.clear(); }
    p.surfaceFeedbackDirty[unit] = true;
}
