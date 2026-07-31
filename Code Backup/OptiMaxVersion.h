// ============================================================================
// THE VERSION. One edit, and it is the three digits below.
//
// This file exists because the version string used to live in TEN places -
// OptiMaxApp.h's CURRENT_VERSION, five fields in resource.rc, and four string
// literals in OptiMax_Profiler.cpp (the profile-export stamp, two tray
// tooltips, and the startup log line). Missing the resource.rc half shipped a
// binary whose file properties advertised the previous version while the app
// reported the new one, and on an unsigned binary metadata that contradicts the
// program is exactly the inconsistency that costs credibility with ML
// classifiers and with anyone reporting a bug against "the version you are
// running". release.ps1 grew a five-site drift guard to catch it; that guard is
// no longer defending against anything, because there is nothing left to drift.
//
// Included by BOTH C++ and resource.rc, so keep it to preprocessor directives -
// no types, no includes, nothing rc.exe cannot parse. Classic include guard
// rather than #pragma once for the same reason.
// ============================================================================
#ifndef OPTIMAX_VERSION_H
#define OPTIMAX_VERSION_H

#define OPTIMAX_VERSION_MAJOR 4
#define OPTIMAX_VERSION_MINOR 3
#define OPTIMAX_VERSION_PATCH 0

// resource.rc's FILEVERSION / PRODUCTVERSION want a comma list. The fourth
// field has always been 0 here and is not part of the released version.
#define OPTIMAX_VERSION_COMMA OPTIMAX_VERSION_MAJOR, OPTIMAX_VERSION_MINOR, OPTIMAX_VERSION_PATCH, 0

#define OPTIMAX_STRINGIFY_(x) #x
#define OPTIMAX_STRINGIFY(x)  OPTIMAX_STRINGIFY_(x)

// "4.2.2" - what the app reports, and what release.ps1 turns into the git tag
// and the contents of version.txt.
#define OPTIMAX_VERSION_STRING  OPTIMAX_STRINGIFY(OPTIMAX_VERSION_MAJOR) "." \
                                OPTIMAX_STRINGIFY(OPTIMAX_VERSION_MINOR) "." \
                                OPTIMAX_STRINGIFY(OPTIMAX_VERSION_PATCH)

// "4.2.2.0" - the four-field form Windows file properties use.
#define OPTIMAX_VERSION_STRING_FULL OPTIMAX_VERSION_STRING ".0"

// Adjacent-literal concatenation is what makes this usable in a wide context:
// in C++ an unprefixed literal adopts the prefix of the one next to it, so
// L"OptiMax Pro v" OPTIMAX_VERSION_STRING is a single wide literal.

#endif // OPTIMAX_VERSION_H
