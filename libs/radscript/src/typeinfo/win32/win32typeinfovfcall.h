#ifndef WIN32TYPEINFOVFCALL_H
#define WIN32TYPEINFOVFCALL_H

int InvokeVf( void * pThat, unsigned int vtbloffset, void * pParams, int numParams );

#if defined( RAD_LINUX ) && defined( __LP64__ )
// x86-64 (System V): integer/pointer and float arguments travel in separate registers; see linuxtypeinfovfcall.cpp
#if defined( __aarch64__ )
enum { INVOKEVF64_INT_REGS = 7, INVOKEVF64_FLOAT_REGS = 8, INVOKEVF64_STACK_SLOTS = 10 };
#else
enum { INVOKEVF64_INT_REGS = 5, INVOKEVF64_FLOAT_REGS = 8, INVOKEVF64_STACK_SLOTS = 10 };
#endif
int InvokeVf64( void * pThat, unsigned int vtbloffset, const uintptr_t * pInts, const float * pFloats,
                const uintptr_t * pStack );
#endif

#endif // WIN32TYPEINFOVFCALL_H