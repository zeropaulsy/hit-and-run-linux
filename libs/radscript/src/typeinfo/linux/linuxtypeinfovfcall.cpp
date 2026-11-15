// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

//=============================================================================
// Linux port of win32/win32typeinfovfcall.cpp: call virtual method number `vtbloffset` of pThat with
// `numParams` 32-bit parameters taken from pParams.
//
// MSVC's thiscall passes `this` in ECX (the original was inline asm). With GCC on i386 a virtual method
// is an ordinary cdecl function taking `this` as its first stack argument, so it can be called through
// a function pointer. Every parameter is one 32-bit stack slot (ints, pointers and floats alike), exactly
// as the original pushed them. NOTE: vtbloffset comes from srrtypes.typ, generated against MSVC's vtable
// layout; it matches GCC's as long as the scripted interfaces have no virtual destructors or overloaded
// virtual methods (the two places the ABIs order slots differently).
//=============================================================================

#include "pch.hpp"
#include <raddebug.hpp>
#include "../win32/win32typeinfovfcall.h"

typedef unsigned int W;   // one 32-bit stack slot

int InvokeVf( void * pThat, unsigned int vtbloffset, void * pParams, int numParams )
{
    void** vtable = *(void***) pThat;
    void*  fn = vtable[ vtbloffset ];
    W*     p = (W*) pParams;

    switch ( numParams )
    {
    case 0: return ((int(*)(void*))fn)( pThat );
    case 1: return ((int(*)(void*,W))fn)( pThat, p[0] );
    case 2: return ((int(*)(void*,W,W))fn)( pThat, p[0], p[1] );
    case 3: return ((int(*)(void*,W,W,W))fn)( pThat, p[0], p[1], p[2] );
    case 4: return ((int(*)(void*,W,W,W,W))fn)( pThat, p[0], p[1], p[2], p[3] );
    case 5: return ((int(*)(void*,W,W,W,W,W))fn)( pThat, p[0], p[1], p[2], p[3], p[4] );
    case 6: return ((int(*)(void*,W,W,W,W,W,W))fn)( pThat, p[0], p[1], p[2], p[3], p[4], p[5] );
    case 7: return ((int(*)(void*,W,W,W,W,W,W,W))fn)( pThat, p[0], p[1], p[2], p[3], p[4], p[5], p[6] );
    case 8: return ((int(*)(void*,W,W,W,W,W,W,W,W))fn)( pThat, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7] );
    case 9: return ((int(*)(void*,W,W,W,W,W,W,W,W,W))fn)( pThat, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8] );
    case 10: return ((int(*)(void*,W,W,W,W,W,W,W,W,W,W))fn)( pThat, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9] );
    default:
        rAssertMsg( false, "InvokeVf: more than 10 script parameters" );
        return 0;
    }
}

#if defined( __LP64__ )
//=============================================================================
// x86-64 (System V): `this` and the first 5 integer/pointer arguments go in RDI, RSI, RDX, RCX, R8, R9, the first
// 8 floats in XMM0-7, everything else on the stack in parameter order, one 8-byte slot each (a float in the low 4
// bytes). Calling through this one signature puts every argument where the method, whatever its real signature,
// looks for it; registers and stack slots it does not use are ignored. TypeInfoMethod::Invoke sorts the arguments.
//=============================================================================
typedef uintptr_t U;
#if defined( __aarch64__ )
//=============================================================================
// Apple arm64: `this` and the first 7 integer/pointer arguments go in X0-X7, the first 8 floats in V0-V7. Stack
// arguments are packed to their natural size, so one signature can't place them: TypeInfoMethod::Invoke refuses a
// method that needs them. No method in srrtypes.typ does (at most 6 parameters).
//=============================================================================
typedef int ( *InvokeFn64 )( void*, U, U, U, U, U, U, U, float, float, float, float, float, float, float, float );

int InvokeVf64( void * pThat, unsigned int vtbloffset, const uintptr_t * i, const float * f, const uintptr_t * )
{
    void** vtable = *(void***) pThat;
    InvokeFn64 fn = (InvokeFn64) vtable[ vtbloffset ];
    return fn( pThat, i[0], i[1], i[2], i[3], i[4], i[5], i[6], f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7] );
}
#else
typedef int ( *InvokeFn64 )( void*, U, U, U, U, U, float, float, float, float, float, float, float, float,
                             U, U, U, U, U, U, U, U, U, U );

int InvokeVf64( void * pThat, unsigned int vtbloffset, const uintptr_t * i, const float * f, const uintptr_t * s )
{
    void** vtable = *(void***) pThat;
    InvokeFn64 fn = (InvokeFn64) vtable[ vtbloffset ];
    return fn( pThat, i[0], i[1], i[2], i[3], i[4], f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7],
               s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9] );
}
#endif
#endif
