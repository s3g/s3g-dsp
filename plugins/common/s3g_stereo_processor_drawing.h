#pragma once
#include "s3g_resonator_encoder_drawing.h"
#include "s3g_circuit_encoder_drawing.h"
#include "s3g_clap_gui_param_queue.h"
#include "vstgui/lib/cclipboard.h"
#include <sstream>

namespace s3g::portable_gui::stereo_drawing {
using namespace resonator_drawing;
using decoder_drawing::calibratedColor;
using circuit_drawing::drawHeaderActionButton;
inline void drawProcessorLabel(const String& name,double x,double y) {
  const auto label=canvas::upper(name);
  const auto box=foundation::rect(x+16.,y-2.,gui_layout::processorControlX(x)-x-24.,15.);
  current->context->setFont(current->font);
  double width=current->context->getStringWidth(label.c_str());
  if(width<=box.getWidth()) current->view.text(label,box,foundation::palette().label);
  else {
    auto face=VSTGUI::owned(new VSTGUI::CFontDesc(current->font->getName(),current->font->getSize()*box.getWidth()/width));
    current->view.text(label,box,foundation::palette().label,VSTGUI::kLeftText,face);
  }
}
inline void drawProcessorSlider(const String& name,const String& value,double norm,
    double y,double x,double w,Attrs labels,Attrs values,const Style& style) {
  ambi_effect_drawing::drawProcessorSlider("",value,norm,y,x,w,labels,values,style);
  drawProcessorLabel(name,x,y);
}
inline void drawProcessorMenu(const String& name,const String& value,double y,
    double x,double w,Attrs labels,Attrs values,const Style& style) {
  ambi_effect_drawing::drawProcessorMenu("",canvas::upper(value),y,x,w,labels,values,style);
  drawProcessorLabel(name,x,y);
}
inline void drawMacroTitleBand(const String& title,const String& preset,
    const String& status,const gui_layout::EncoderTitleBand& band,const Style& style) {
  auto titles=softTitleAttrs(),labels=softLabelAttrs(),values=softValueAttrs();
  drawAtPoint_withAttributes(title,makePoint(band.titleX,band.titleY),titles);
  drawMenu("",canvas::upper(preset),band.controlY,labels,values,style,
           band.presetLabelX,band.presetMenu.x,band.presetMenu.width);
  drawAtPoint_withAttributes("PRESET",makePoint(band.presetLabelX,band.controlY+1.),labels);
  drawHeaderActionButton(cocoaRect(band.loadButton),cocoaRect(band.loadButton),"LOAD",labels,style);
  drawHeaderActionButton(cocoaRect(band.saveButton),cocoaRect(band.saveButton),"SAVE",labels,style);
  if(!status.empty()) drawRightStatus(status,band.canvas.width,band.titleY,values,band.statusRightInset);
}
inline Rect offset(Rect r, double x, double y) {
  r.origin.x += x; r.origin.y += y; return r;
}
inline void drawCenteredTextToFit(const String& text, Rect box, Attrs attrs) {
  auto size = sizeWithAttributes(text, attrs);
  if (size.width > box.size.width && size.width > 0) {
    attrs.size = foundation::fontMetrics().body * box.size.width / size.width;
    size = sizeWithAttributes(text, attrs);
  }
  drawAtPoint_withAttributes(text,
    makePoint(box.origin.x+(box.size.width-size.width)*.5,
              box.origin.y+(box.size.height-size.height)*.5), attrs);
}
}
