s3g::portable_gui::foundation::EditorHost *portableGuiEditor = nullptr;
uint32_t portableGuiWidth = kGuiWidth, portableGuiHeight = kGuiHeight;
#if defined(S3G_ROUTING_GUI_VISIBLE_ATOMIC)
std::atomic<bool> portableGuiVisible { false };
#else
bool portableGuiVisible = false;
#endif
const clap_host_params_t *hostParams = nullptr;
// Enough space to announce an entire 64-source preset transactionally.
s3g::clap_gui::ParamEventQueue<4096> guiParamEvents;
