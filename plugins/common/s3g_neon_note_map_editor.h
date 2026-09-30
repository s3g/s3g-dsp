#pragma once
#include "s3g_vstgui_canvas.h"
#include "s3g_neon_note_map.h"
#include "vstgui/lib/cdropsource.h"

namespace s3g::portable_gui::canvas {
// Shared, main-thread-only draft editor. Apply is atomic; invalid lists and
// duplicate cell addresses never alter the sounding map. Keyboard pitch maps
// opt into duplicate keys and OFF explicitly. All 32 fields stay visible.
class NeonNoteMapEditor {
public:
    controller::neon_midi::NoteMap draft;
    bool open = false;
    unsigned base = 36;
    bool keyboard = false;
    controller::neon_midi::PadNotes resetNotes {};
    std::string error;
    void begin(controller::neon_midi::NoteMap value, unsigned start,
        const controller::neon_midi::PadNotes* keyboardReset = nullptr) {
        draft = value; base = start; draft.notes = value.resolved(base);
        keyboard = keyboardReset != nullptr;
        if (keyboardReset) resetNotes = *keyboardReset;
        error.clear(); open = true;
    }
    void paint(View& view, double x, double y, double width,
        std::function<void(controller::neon_midi::NoteMap)> apply,
        bool enabled = true) {
        using namespace controller::neon_midi;
        const auto palette = foundation::palette();
        const double column = (width - 16) / 4.;
        for (unsigned bank = 0; bank < 4; ++bank) {
            const double left = x + 16 + column * bank;
            view.text(format("%c", 'A' + bank), rect(left, y, column-6, 18), palette.label, kCenterText);
            for (unsigned pad = 0; pad < 8; ++pad) {
                const unsigned cell = bank * 8 + pad;
                const auto box = rect(left, y+24+pad*24, column-6, 15);
                if (!bank) view.text(format("%u", pad+1), rect(x, box.top-2, 12, 19), palette.label);
                view.fill(box, palette.cell);
                view.text(draft.notes[cell] == 255 ? "OFF" : format("%u", draft.notes[cell]), box, palette.value, kCenterText);
                view.number(box, draft.notes[cell] == 255 ? -1 : draft.notes[cell], [this, cell](double value) {
                    if (value < (keyboard ? -1 : 0) || value > 127 || value != std::round(value)) { error = keyboard ? "NOTES 0-127 / -1 OFF" : "USE WHOLE NOTES 0-127"; return; }
                    draft.notes[cell] = value < 0 ? 255 : static_cast<uint8_t>(value); draft.custom = true;
                    error.clear();
                }, enabled);
            }
        }
        const double buttonWidth = (width-8)/2.;
        view.button(rect(x, y+220, buttonWidth, 20), "COPY LIST", [&view, this] {
            if (!view.getFrame()) return;
            std::string list;
            for (unsigned i=0;i<32;++i) list += format("%d%s",draft.notes[i] == 255 ? -1 : int(draft.notes[i]), i==31 ? "" : (i%8==7 ? "\n" : ", "));
            view.getFrame()->setClipboard(CDropSource::create(list.data(), static_cast<uint32_t>(list.size()), IDataPackage::kText));
        });
        view.button(rect(x+buttonWidth+8, y+220, buttonWidth, 20), "PASTE LIST", [&view, this] {
            auto clipboard = view.getFrame() ? view.getFrame()->getClipboard() : nullptr;
            PadNotes notes;
            if (clipboard) for (uint32_t i=0;i<clipboard->getCount();++i) {
                const void* bytes = nullptr; IDataPackage::Type type;
                auto size = clipboard->getData(i,bytes,type);
                if (type != IDataPackage::kText || !bytes || size > 4096) continue;
                const auto* text = static_cast<const char*>(bytes);
                if (size && !text[size-1]) --size;
                if (parseNotes({text,size},notes,!keyboard,keyboard)) { draft.notes=notes; draft.custom=true; error.clear(); return; }
            }
            error = keyboard ? "PASTE 32 NOTES 0-127 / -1 OFF" : "PASTE 32 UNIQUE NOTES 0-127";
        }, false, enabled);
        view.button(rect(x, y+248, buttonWidth, 20), keyboard ? "FROM SCALE" : "DEFAULT", [this] {
            draft.custom=keyboard; draft.notes=keyboard ? resetNotes : sequentialNotes(base); error.clear();
        }, !draft.custom, enabled);
        view.button(rect(x+buttonWidth+8, y+248, buttonWidth, 20), "FROM A1", [this] {
            if (draft.notes[0] > 96) { error="A1 MUST BE 0-96 FOR RUN"; return; }
            draft.notes=sequentialNotes(draft.notes[0]); draft.custom=true; error.clear();
        }, false, enabled);
        const bool valid = validNotes(draft.notes,!keyboard,keyboard) && error.empty();
        view.text(!error.empty() ? error : !valid ? "EACH PAD NEEDS A UNIQUE NOTE" : enabled ? keyboard ? "DUPLICATES OK / -1 OFF / APPLY SETS MANUAL" : "A1-A8 / B1-B8 / C1-C8 / D1-D8" : "EDIT THE MAP IN UTILITY",
            rect(x,y+276,width,18), palette.label);
        view.button(rect(x,y+304,buttonWidth,20), "CANCEL", [this] { open=false; });
        view.button(rect(x+buttonWidth+8,y+304,buttonWidth,20), "APPLY", [this,apply] {
            if (!controller::neon_midi::validNotes(draft.notes,!keyboard,keyboard) || !error.empty()) return;
            apply(draft); open=false;
        }, false, enabled && valid);
    }
};
} // namespace s3g::portable_gui::canvas
