#pragma once
#include <clap/clap.h>

namespace s3g::gui_documentation {
// Private, main-thread-only bridge for the existing documentation harness.
// It is exposed only when S3G_GUI_DOCUMENTATION_CAPTURE=1, never to normal
// hosts.
inline constexpr char kExtension[] = "org.s3g.gui-documentation/1";
struct Extension {
  bool (*loadFixtures)(const clap_plugin_t *);
  bool (*selectPage)(const clap_plugin_t *, uint32_t);
  bool (*exerciseMutation)(const clap_plugin_t *);
};
} // namespace s3g::gui_documentation
