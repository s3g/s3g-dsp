// Headers assumed transitively by VSTGUI's MSVC build but required explicitly
// by current MinGW-w64/libstdc++ releases.

#pragma once

// MinGW's Windows headers redefine WIN32 as an empty compatibility macro.
// VSTGUI evaluates WIN32 arithmetically, so load the headers once and restore
// the compiler's numeric spelling before any VSTGUI header is parsed.
#if defined(_WIN32)
#ifndef UNICODE
#define UNICODE 1
#endif
#ifndef _UNICODE
#define _UNICODE 1
#endif
#include <windows.h>
#ifdef WIN32
#undef WIN32
#endif
#define WIN32 1
// The 16.2 headers still expose the obsolete memory-model spellings as empty
// macros. They collide with ordinary C++ identifiers in the DSP sources and
// are not used by VSTGUI or the suite.
#ifdef far
#undef far
#endif
#ifdef near
#undef near
#endif
#endif

#include <locale>
