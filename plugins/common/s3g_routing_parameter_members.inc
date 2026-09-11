#include "s3g_routing_gui_members.inc"
// Parameter IDs are stable CLAP IDs, not display order. Values are published
// by process/flush and read by the portable editor without touching live DSP.
std::array<std::atomic<double>, 4096 + 64> routingValues{};
std::atomic<float> routingPhase{0.f};
