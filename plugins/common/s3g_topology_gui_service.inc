void destroyPortableGui(Plugin &);

void servicePortableParamEvents(Plugin &p, const clap_output_events_t *out) {
  // These processors already have a lock-free GUI -> DSP parameter bank.
  // GUI edits publish there immediately, as Cocoa did. This queue reports
  // the edits to the host; replaying old values into the bank while draining
  // would roll back newer automation when a host applies backpressure.
  s3g::clap_gui::serviceParamEvents(p.guiParamEvents, out,
                                    [](clap_id, double) {});
}
