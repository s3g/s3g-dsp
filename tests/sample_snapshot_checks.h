#pragma once
#include <clap/clap.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace sample_snapshot_test {
struct Buffer {
    std::vector<uint8_t> bytes;
    size_t cursor=0;
    clap_ostream_t output{this,[](const clap_ostream_t* s,const void* data,uint64_t size)->int64_t{
        auto& b=*static_cast<Buffer*>(s->ctx);const auto* first=static_cast<const uint8_t*>(data);
        b.bytes.insert(b.bytes.end(),first,first+size);return size;}};
    clap_istream_t input{this,[](const clap_istream_t* s,void* data,uint64_t size)->int64_t{
        auto& b=*static_cast<Buffer*>(s->ctx);size=std::min<uint64_t>(size,b.bytes.size()-b.cursor);
        std::memcpy(data,b.bytes.data()+b.cursor,size);b.cursor+=size;return size;}};
};
// Repeated host-undo requests must not grow or mutate a settled snapshot.
// Disk-work exclusion is checked independently by the source-call audit and
// Decks' owning-project integration fixture; small state alone proves no such thing.
inline bool repeated(const clap_plugin_t* plugin,const clap_plugin_state_t* state,size_t expected) {
    if(!plugin||!state)return false;
    Buffer first;
    if(!state->save(plugin,&first.output)||first.bytes.size()!=expected)return false;
    for(unsigned n=0;n<32;++n){Buffer next;
        if(!state->save(plugin,&next.output)||next.bytes!=first.bytes){
            std::cerr<<"Unstable/expanded sample snapshot: "<<plugin->desc->id<<'\n';return false;}}
    return true;
}
// Exercise Project references and pathless safety PCM using each product's
// current serialized header. Restore the original fixture before playback tests.
template<class Header,class Edit>
bool referenceCase(const clap_plugin_t* plugin,const clap_plugin_state_t* state,
    const std::vector<uint8_t>& original,Edit edit) {
    if(original.size()<sizeof(Header))return false;
    Buffer linked;linked.bytes=original;Header header;
    std::memcpy(&header,linked.bytes.data(),sizeof(header));edit(header);
    std::memcpy(linked.bytes.data(),&header,sizeof(header));
    const bool compact=state->load(plugin,&linked.input)&&repeated(plugin,state,sizeof(Header));
    Buffer restore;restore.bytes=original;
    const bool restored=state->load(plugin,&restore.input)&&repeated(plugin,state,original.size());
    if(!compact||!restored)std::cerr<<"Project reference / PCM safety snapshots failed: "<<plugin->desc->id<<'\n';
    return compact&&restored;
}
} // namespace sample_snapshot_test
