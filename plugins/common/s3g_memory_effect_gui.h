#pragma once

#include "s3g_clap_gui_param_queue.h"
#include "s3g_clap_vstgui.h"
#include "s3g_topology_colors.h"
#include "s3g_vstgui_canvas.h"
#include <memory>

namespace s3g::clap_gui {
enum class MemoryEffectAction : uint8_t { Parameter, Capture, Clear };
struct MemoryEffectEvent : ParamEvent {
  MemoryEffectAction action = MemoryEffectAction::Parameter;
};
class MemoryEffectQueue : public SpscEventQueue<MemoryEffectEvent, 1024u> {
public:
  bool push(const ParamEvent &event) { return Base::push({event}); }
  bool pushAction(MemoryEffectAction action) {
    return Base::push({{}, action});
  }
  bool pushBatch(const ParamEvent *events, uint32_t count) {
    if (!events || count > 1023u)
      return false;
    std::array<MemoryEffectEvent, 1023u> batch{};
    for (uint32_t i = 0; i < count; ++i)
      batch[i] = {events[i]};
    return Base::pushBatch(batch.data(), count);
  }

private:
  using Base = SpscEventQueue<MemoryEffectEvent, 1024u>;
};
} // namespace s3g::clap_gui
