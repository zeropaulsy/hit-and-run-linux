//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


//============================================================================
// Include Files
//============================================================================

#include "pch.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <radtypeinfo.hpp>

#if defined (RAD_WIN32) || defined (RAD_XBOX)
#include "win32/win32typeinfovfcall.h"
#endif

#ifdef RAD_PS2
#include "ps2/ps2typeinfovfcall.h"
#endif

#ifdef RAD_GAMECUBE
#include "gcn/gcntypeinfovfcall.h"
#endif

//============================================================================
// Component: IRadTypeInfoMethod
//============================================================================

//========================================================================
// IRadTypeInfoMethod::Invoke
//========================================================================
unsigned int IRadTypeInfoMethod::Invoke
(
    void * pI,
    void * pParams,
    unsigned int numParams
) 
{
    if ( pI == NULL )
    {
       rDebugString( "IRadTypeInfoMethod: Invoke() called on NULL object!\n" ); 
    }
    else
    {
        if ( numParams != m_lstParamsPtr->GetSize( ) )
        {
            rDebugString( "IRadTypeInfoMethod: [" );
            rDebugString( GetName( ) );
            rDebugString( "] was called incorrectly!\n" );
        }
        else
        {
            //
            //  ---- Linux x86-64 ----------------------------------------------------------
            //
            // Linux port: pParams holds one pointer-sized slot per parameter. Integers and pointers go in the
            // general registers (5 after `this`), floats in the SSE registers (8), the rest on the stack in
            // parameter order: sort them by the declared types, as the PS2 and GameCube paths do.

            #if defined( RAD_LINUX ) && defined( __LP64__ )

                uintptr_t   intRegs[ INVOKEVF64_INT_REGS ] = { 0 };
                float       floatRegs[ INVOKEVF64_FLOAT_REGS ] = { 0 };
                uintptr_t   stack[ INVOKEVF64_STACK_SLOTS ] = { 0 };
                int         ip = 0, fp = 0, sp = 0;
                uintptr_t*  slots = (uintptr_t*) pParams;

                for( unsigned int i = 0; i < numParams; i ++ )
                {
                    bool isFloat = GetParamInfoAt( i )->GetIndLvl( ) == 0 && GetParamInfoAt( i )->GetHashedType( ) == ParserConst::TOK_FLOAT;
                    if ( isFloat && fp < INVOKEVF64_FLOAT_REGS )
                    {
                        floatRegs[ fp++ ] = *(float*) &slots[ i ];
                    }
                    else if ( !isFloat && ip < INVOKEVF64_INT_REGS )
                    {
                        intRegs[ ip++ ] = slots[ i ];
                    }
                    else
                    {
                        rAssertMsg( sp < INVOKEVF64_STACK_SLOTS, "InvokeVf64: too many script parameters" );
                        if ( sp < INVOKEVF64_STACK_SLOTS )
                        {
                            stack[ sp++ ] = isFloat ? (uintptr_t) *(unsigned int*) &slots[ i ] : slots[ i ];
                        }
                    }
                }

                #if defined( __aarch64__ )
                if ( sp > 0 )
                {
                    fprintf( stderr, "InvokeVf64: %s needs stack arguments, not supported on arm64\n", GetName( ) );
                    abort( );
                }
                #endif

                int ret = InvokeVf64( pI, m_VTableOffset, intRegs, floatRegs, stack );

                if ( GetReturnInfo( )->GetHashedType( ) == ParserConst::TOK_BOOL )
                {
                    return ret & 0x000000FF;    // a bool comes back in AL, the rest of EAX is undefined
                }
                return ret;

            //
            //  ---- RAD_WIN32 or RAD_XBOX ----------------------------------------------------
            // 
                 
            #elif defined (RAD_WIN32) || defined (RAD_XBOX)

                return InvokeVf( pI, m_VTableOffset, pParams, numParams );

            #endif
           
            //
            //  ---- RAD_PS2 ----------------------------------------------------
            //
           
            #ifdef RAD_PS2        
           
                unsigned int pIntParams[ 7 ];
                float        pFloatParams[ 8 ];

                int          ip = 0;
                int          fp = 0;

                for( unsigned int i = 0; i < numParams; i ++ )
                {
                    if ( GetParamInfoAt( i )->GetIndLvl( ) > 0 || GetParamInfoAt( i )->GetHashedType( ) != ParserConst::TOK_FLOAT )
                    {
                        rAssert( ip < 7 );                   
                        pIntParams[ ip++ ] = ((unsigned int*)pParams)[i];                            
                    }
                    else
                    {
                        rAssert( fp < 8 );
                        pFloatParams[ fp++ ] = ((float*)pParams)[ i ];
                    }
                }

                int ret = InvokeVf( pI, m_VTableOffset + 1, pIntParams, pFloatParams ); 
            
                if ( GetReturnInfo( )->GetHashedType( ) == ParserConst::TOK_BOOL )
                {
                    return ret & 0x000000FF;
                }
            
                return ret;
            
            #endif
                
            //
            //  ---- RAD_GAMECUBE ------------------------------------------------------
            //                    
            
            #ifdef RAD_GAMECUBE            
           
                unsigned int pIntParams[ 7 ];
                float        pFloatParams[ 8 ];

                int          ip = 0;
                int          fp = 0;

                for( unsigned int i = 0; i < numParams; i ++ )
                {
                    if ( GetParamInfoAt( i )->GetIndLvl( ) > 0 || GetParamInfoAt( i )->GetHashedType( ) != ParserConst::TOK_FLOAT )
                    {
                        rAssert( ip < 7 );

                        pIntParams[ ip++ ] = ((unsigned int*)pParams)[ i ];
                    }
                    else
                    {
                        rAssert( fp < 8 );
                        pFloatParams[ fp++ ] = ((float*)pParams)[ i ];
                    }
                }
            
                int ret = InvokeVf( pI, m_VTableOffset + 1, pIntParams, pFloatParams );

                return ret;
            
            #endif          
       }
   }
   
   return 0; 
}


