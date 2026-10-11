#pragma once

// Included first by every translation unit whose float results are part of a Cactus program's meaning.
// `-ffp-contract=fast` overrides the pragma and sets no macro, so builds must not pass it.

#if !defined(__clang__)
#error "Cactus code needs Clang: it relies on `#pragma clang fp contract(off)` to keep float results identical"
#endif

#if defined(__FAST_MATH__)
#error "Cactus code must not be built with fast-math: it changes float results"
#endif

#pragma clang fp contract(off)
