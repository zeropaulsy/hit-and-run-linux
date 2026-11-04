/* Mach-O counterpart of gen_cmake.py's objcopy step, force-included into pddigl: the renderer's operator new/delete
   references bind to its private heap (port/compat/pddi_heap.cpp), not the game's. */
#ifndef SHAR_COMPAT_MACOS_PDDI_NEW_H
#define SHAR_COMPAT_MACOS_PDDI_NEW_H
#ifdef __cplusplus
#include <stddef.h>
void* operator new( size_t ) __asm__( "_pddi_heap_new" );
void* operator new[]( size_t ) __asm__( "_pddi_heap_new_array" );
void operator delete( void* ) throw() __asm__( "_pddi_heap_delete" );
void operator delete[]( void* ) throw() __asm__( "_pddi_heap_delete_array" );
#endif
#endif
