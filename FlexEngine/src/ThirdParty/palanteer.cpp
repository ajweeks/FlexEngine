// Palanteer implementation lives in its own translation unit so it can be compiled
// without the precompiled header (which already includes palanteer.h without PL_IMPLEMENTATION)
#define PL_IMPLEMENTATION 1
#define PL_IMPL_COLLECTION_BUFFER_BYTE_QTY 50'000'000

#include "palanteer/palanteer.h"
