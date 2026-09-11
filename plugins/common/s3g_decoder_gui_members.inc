s3g::portable_gui::foundation::EditorHost *portableGuiEditor = nullptr;
uint32_t portableGuiWidth = kGuiWidth, portableGuiHeight = kGuiHeight;
bool portableGuiVisible = false;
const clap_host_params_t *hostParams = nullptr;
s3g::clap_gui::ParamEventQueue<4096> guiParamEvents;
