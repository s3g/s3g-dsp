#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace s3g::decks {
// Display-only settings. Never read by DSP or used to select a MIDI deck.
struct DeckViewState {
    std::array<uint8_t,2> oppositeEdit {},page {{1,1}},view {},channels {},orientation {};
    std::array<double,2> zoom {{4,4}},offset {};
    bool valid() const noexcept {
        for(unsigned d=0;d<2;++d)if(oppositeEdit[d]>1||page[d]>6||view[d]>3||channels[d]>2||orientation[d]>3
            ||!std::isfinite(zoom[d])||zoom[d]<1||zoom[d]>64||!std::isfinite(offset[d])||offset[d]<0||offset[d]>1)return false;
        return true;
    }
    unsigned target(unsigned side) const noexcept {return oppositeEdit[side]?1-side:side;}
    bool vertical(unsigned deck) const noexcept {return orientation[deck]==0||orientation[deck]==3;}
    bool scrolling(unsigned deck) const noexcept {return orientation[deck]<2;}
};
struct DeckViewport {
    double start=0,width=1;
    static DeckViewport make(double zoom,double offset,double cursor,bool scroll) noexcept {
        const double width=1/std::clamp(zoom,1.,64.);
        // Do not clamp the scrolling view to the file edges: the playhead stays
        // fixed at 40%, including at the beginning/end of a source.
        return {scroll&&cursor>=0 ? cursor-width*.4 : std::clamp(offset,0.,1-width),width};
    }
    double at(double fraction) const noexcept {return start+fraction*width;}
    double fraction(double source) const noexcept {return (source-start)/width;}
    double pan(double delta) const noexcept {return std::clamp(start-delta*width*.1,0.,1-width);}
};
struct DeckLayout {
    static constexpr double panelWidth=604,rowPitch=26;
    static constexpr double left(unsigned side){return side?818:18;}
    static constexpr double waveTop=158,waveHeight=372,pathTop=567,pathHeight=64;
    static constexpr double performanceTop=660,padTop=722;
};
}
