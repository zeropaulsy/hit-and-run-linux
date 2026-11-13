// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Private heap for the renderer (PDDI). On Windows the renderer was a DLL (pddidx8.dll) with its own C
// runtime, so its allocations never touched the game's fixed-size heaps (and texture data lived in
// Direct3D's managed memory). The statically linked GL renderer gets the same separation: after pddigl is
// built, its operator new/delete references are renamed to these functions (see port/gen_cmake.py).

#include <stdlib.h>
#include <string.h>

extern "C" void* pddi_heap_new(size_t size)
{
    void* p = malloc(size ? size : 1);
    if(!p) abort();

    // debug: SHAR_FILL_PDDI=<byte> fills the renderer's new allocations to expose uninitialised reads
    // (SHAR_FILL_ALLOC does the same for the game's heaps)
    static int fill = -2;
    if(fill == -2)
    {
        const char* e = getenv("SHAR_FILL_PDDI");
        fill = e ? (int)(strtol(e, NULL, 0) & 0xff) : -1;
    }
    if(fill >= 0)
        memset(p, fill, size);
    return p;
}

extern "C" void* pddi_heap_new_array(size_t size)        { return pddi_heap_new(size); }
extern "C" void  pddi_heap_delete(void* p)               { free(p); }
extern "C" void  pddi_heap_delete_array(void* p)         { free(p); }
