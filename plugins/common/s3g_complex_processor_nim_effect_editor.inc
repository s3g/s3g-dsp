std::string error;
bool altDown = false;
drawing::Rect bounds() const { return drawing::makeRect(0, 0, 454, 292); }
drawing::Point convertPoint_fromView(drawing::Point p, void *) { return p; }
void setNeedsDisplay(bool) { invalid(); }
void focus() {
  if (getFrame())
    getFrame()->setFocusView(this);
}
bool wantsFocus() const override { return true; }
void paint() override {
  drawing::DrawingScope scope(*this, context, font, titleFont, &error);
  drawOriginal(bounds());
}
void service() override { invalid(); }
void stopRefresh() override {
  if (apply)
    apply(CLAP_INVALID_ID, 0., false);
  _dragParam = CLAP_INVALID_ID;
  Canvas::stopRefresh();
}
~EffectEditor() override { stopRefresh(); }
bool pointerDown(VSTGUI::MouseDownEvent &e) override {
  if (!e.buttonState.isLeft())
    return false;
  if (apply)
    apply(CLAP_INVALID_ID, 0., false);
  drawing::Event event{{e.mousePosition.x, e.mousePosition.y},
                       int(e.clickCount)};
  mouseDown(&event);
  return true;
}
void pointerMove(VSTGUI::MouseMoveEvent &e) override {
  if (e.buttonState.isLeft()) {
    drawing::Event event{{e.mousePosition.x, e.mousePosition.y}, 1};
    mouseDragged(&event);
  }
}
void pointerUp(VSTGUI::MouseUpEvent &e) override {
  drawing::Event event{{e.mousePosition.x, e.mousePosition.y}, 1};
  mouseUp(&event);
  if (apply)
    apply(CLAP_INVALID_ID, 0., false);
}
