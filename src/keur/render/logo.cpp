/**
 * @file render/logo.cpp
 * @brief bucket_for_width — pure terminal-width -> size bucket. The logo strings
 *        themselves are baked blobs in logo_generated.cpp (scripts/gen-logo.sh).
 *        @implements KEUR-LOGO.bucket
 */
#include "render/logo.h"

namespace keur::render {

Bucket bucket_for_width(int cols) {
    if (cols <= 60) return Bucket::Sm;
    if (cols <= 100) return Bucket::Md;
    if (cols <= 140) return Bucket::Lg;
    return Bucket::Xl;
}

}  // namespace keur::render
