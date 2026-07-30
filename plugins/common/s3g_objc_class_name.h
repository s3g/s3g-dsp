#pragma once

// Objective-C class names are process-global, even when their implementations
// live in separate CLAP bundles. Expand compile-time product traits into the
// class token whenever one source file builds more than one bundle.
#define S3G_OBJC_CLASS_JOIN_INNER(prefix, suffix) prefix##suffix
#define S3G_OBJC_CLASS_JOIN(prefix, suffix) \
    S3G_OBJC_CLASS_JOIN_INNER(prefix, suffix)
