// Included in each topology Plugin. Only the atomic visibility flag and
// SPSC event queue cross the GUI/audio boundary; EditorHost is main-thread
// owned.
s3g::portable_gui::foundation::EditorHost *portableGuiEditor = nullptr;
uint32_t portableGuiWidth = kGuiWidth;
uint32_t portableGuiHeight = kGuiHeight;
std::atomic<bool> portableGuiVisible{false};
const clap_host_params_t *hostParams = nullptr;
s3g::clap_gui::ParamEventQueue<512> guiParamEvents;
