// Main-thread-only editing model. It never processes audio.
std::unique_ptr<Plugin> monitorModel;
std::array<std::atomic<double>, kParamCount> monitorValues{};
std::atomic<bool> monitorClearNegative{false};
const clap_host_params_t *monitorHostParams = nullptr;
const clap_host_state_t *monitorHostState = nullptr;
s3g::clap_gui::ParamEventQueue<1024> monitorGuiEvents;
// SPSC latest-value mailbox: the producer and consumer each own a slot,
// exchanging ownership through the middle slot. No locks or allocation.
std::array<SavedState, 3> monitorRouting{};
unsigned monitorWriteSlot = 0, monitorReadSlot = 2;
std::atomic<unsigned> monitorMiddleSlot{1};
s3g::portable_gui::foundation::EditorHost *portableGuiEditor = nullptr;
uint32_t portableGuiWidth = kGuiWidth, portableGuiHeight = kGuiHeight;
bool portableGuiVisible = false;
