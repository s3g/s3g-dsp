#pragma once
#include "s3g/tracker/clap_midi_engine.h"
#include <clap/clap.h>
#include <memory>
namespace s3g::tracker {
class WindowsTrackerEditor {
public:
    WindowsTrackerEditor(midi::Engine&, const clap_host_t*);
    ~WindowsTrackerEditor();
    bool ready() const;
    bool setParent(void*);
    bool setSize(uint32_t, uint32_t);
    bool setVisible(bool);
    void applyDocument(const ProjectDocument&);
    ProjectDocument currentDocument();
    void flush();
    void cancelPublication();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
