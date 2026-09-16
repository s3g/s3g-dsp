#include "windows_pyro_coefficients_api.h"
#if defined(S3G_WINDOWS_TEST_REFERENCE)
#define S3G_WINDOWS_PYRO_COEFFICIENTS_REFERENCE
#define S3G_WINDOWS_LISTENER_NORMALIZATION_REFERENCE
#define s3g s3g_pyro_reference
#define PYRO_NAME(name) pyro_reference_##name
#else
#define PYRO_NAME(name) pyro_candidate_##name
#endif
#include "s3g_ambi_pyrosphere_presets.h"

extern "C" {
void* PYRO_NAME(create)(double rate) {
    auto* p = new s3g::AmbiPyrosphereEncoder; p->prepare(rate); return p;
}
void PYRO_NAME(destroy)(void* p) { delete static_cast<s3g::AmbiPyrosphereEncoder*>(p); }
void PYRO_NAME(configure)(void* raw, unsigned preset, unsigned voices, unsigned order, unsigned variant) {
    auto* p = static_cast<s3g::AmbiPyrosphereEncoder*>(raw);
    auto params = s3g::ambiPyrosphereFactoryPreset(preset);
    params.voices = voices; params.order = order;
    if (variant) {
        const float x = float(variant % 17) / 16.0f;
        params.body=x; params.air=1.0f-x; params.sweep=x*0.7f;
        params.shrill=x*0.8f; params.q=1.0f-x; params.particles=x;
        params.pressure=x; params.gustDepth=1.0f-x; params.material=x;
        params.fieldListenMode=static_cast<s3g::AmbiFieldListenMode>(variant%4);
        params.fieldListenAmount=0.8f;
    }
    p->setParams(params); p->beginTransition();
}
void PYRO_NAME(reset)(void* p) { static_cast<s3g::AmbiPyrosphereEncoder*>(p)->reset(); }
void PYRO_NAME(process)(void* p, float* const* output, unsigned channels, unsigned frames) {
    static_cast<s3g::AmbiPyrosphereEncoder*>(p)->process(output,channels,frames);
}
unsigned PYRO_NAME(inspect)(void* raw, double* out) {
    auto& p=*static_cast<s3g::AmbiPyrosphereEncoder*>(raw); unsigned i=0;
    out[i++]=p.combustionLayerEnergy(); out[i++]=p.jetLayerEnergy();
    out[i++]=p.planetaryLayerEnergy(); out[i++]=p.planetaryModalLayerEnergy();
    out[i++]=double(p.geologicalEventCount()); out[i++]=double(p.spallEventCount());
    out[i++]=double(p.collapseEventCount()); out[i++]=double(p.ignitionEventCount());
    out[i++]=double(p.structuralSnapEventCount()); out[i++]=double(p.fallEventCount());
    out[i++]=double(p.planetaryBlisterEventCount()); out[i++]=double(p.planetaryFrontEventCount());
    out[i++]=double(p.planetaryVentEventCount()); out[i++]=double(p.planetaryLatticeEventCount());
    out[i++]=double(p.modalExcitationEventCount()); out[i++]=p.scoreActivity();
    out[i++]=double(p.scoreArcCount()); out[i++]=double(p.scoreCascadeCount());
    out[i++]=double(p.scoreConsequenceCount());
    for(unsigned v=0;v<64;++v){out[i++]=p.voiceEnergy(v);out[i++]=p.voiceGustLevel(v);}
    return i;
}
}
