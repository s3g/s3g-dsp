#pragma once
#include <cstdint>
extern "C" {
#define S3G_DECLARE_PYRO(prefix) \
void* prefix##_create(double); \
void prefix##_destroy(void*); \
void prefix##_configure(void*, unsigned, unsigned, unsigned, unsigned); \
void prefix##_reset(void*); \
void prefix##_process(void*, float* const*, unsigned, unsigned); \
unsigned prefix##_inspect(void*, double*);
S3G_DECLARE_PYRO(pyro_reference)
S3G_DECLARE_PYRO(pyro_candidate)
#undef S3G_DECLARE_PYRO
}
