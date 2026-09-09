#pragma once
#include "s3g_clap_gui_param_queue.h"

namespace s3g::clap_gui {
// A preset's parameter batch and delay/filter reset travel together to the
// audio thread. The reset is internal and is never emitted as a CLAP event.
struct DrumEffectEvent : ParamEvent {
  bool resetDsp = false;
};
class DrumEffectQueue : public SpscEventQueue<DrumEffectEvent, 1024u> {
public:
  bool push(const ParamEvent &event) { return Base::push({event, false}); }
  bool pushBatch(const ParamEvent *events, uint32_t count, bool reset = false) {
    if (!events || count > 1022u)
      return false;
    std::array<DrumEffectEvent, 1023u> batch{};
    for (uint32_t i = 0; i < count; ++i)
      batch[i] = {events[i], false};
    if (reset)
      batch[count++] = {{}, true};
    return Base::pushBatch(batch.data(), count);
  }

private:
  using Base = SpscEventQueue<DrumEffectEvent, 1024u>;
};
} // namespace s3g::clap_gui
