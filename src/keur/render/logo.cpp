/**
 * @file render/logo.cpp
 * @brief bucket_for_width — pure terminal-width -> size bucket. The logo strings
 *        themselves are baked blobs in logo_generated.cpp (scripts/gen-logo.sh).
 *        @implements KEUR-LOGO.bucket
 */
#include "render/logo.h"

#include <initializer_list>

namespace keur::render {

Bucket bucket_for_width(int cols) {
    if (cols <= 60) return Bucket::Sm;
    if (cols <= 100) return Bucket::Md;
    if (cols <= 140) return Bucket::Lg;
    return Bucket::Xl;
}

// Rendered logo heights (rows) per bucket — kept in sync with scripts/gen-logo.sh
// (sm=48/md=72/lg=96/xl=128 braille cols produce these row counts). If gen-logo
// changes the buckets, update this table (the e2e/height tests guard it).
int logo_height(Bucket b) {
    switch (b) {
        case Bucket::Sm: return 13;
        case Bucket::Md: return 20;
        case Bucket::Lg: return 27;
        case Bucket::Xl: return 36;
    }
    return 20;
}

// Minimum braille width each bucket needs to stay legible (its source width).
static int logo_min_cols(Bucket b) {
    switch (b) {
        case Bucket::Sm: return 48;
        case Bucket::Md: return 72;
        case Bucket::Lg: return 96;
        case Bucket::Xl: return 128;
    }
    return 72;
}

Bucket bucket_for_size(int cols, int rows_available) {
    // Try biggest first; take the first that fits BOTH axes. Width: the bucket's
    // braille columns must not exceed the terminal. Height: its logo rows must
    // fit the rows left after the level-select UI. Sm is the floor so a tiny
    // terminal still shows a (clipped-but-recognisable) logo rather than nothing.
    for (Bucket b : {Bucket::Xl, Bucket::Lg, Bucket::Md, Bucket::Sm}) {
        if (logo_min_cols(b) <= cols && logo_height(b) <= rows_available)
            return b;
    }
    return Bucket::Sm;
}

}  // namespace keur::render
