//=============================================================================
// Linux port: the debug-communication system (connection to Radical's PC developer tools over Winsock
// with WSAAsyncSelect window messages) is not ported. As documented in raddebugcommunication.hpp,
// channel creation returns NULL when the system is unavailable, and every client handles that.
//=============================================================================

#include "pch.hpp"
#include <raddebugcommunication.hpp>
#include <pthread.h>

#ifdef __APPLE__
static pthread_mutex_t s_DbgComMutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
#else
static pthread_mutex_t s_DbgComMutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
#endif

void radDbgComHostGetTargetTable( IRadDbgComTargetTable** pIRadDbgComTargetTable )
{
    *pIRadDbgComTargetTable = NULL;
}

void radDbgComHostCreateChannel( const radDbgComTargetName pName, unsigned short protocol,
                                 IRadDbgComChannel** ppChannel )
{
    *ppChannel = NULL;
}

void radDbgComHostService( void )
{
}

void radDbgComTargetInitialize( radDbgComType targetType, unsigned short port, void* pInitInfo,
                                radMemoryAllocator alloc )
{
}

void radDbgComTargetTerminate( void )
{
}

void radDbgComService( void )
{
}

void radDbgComTargetCreateChannel( unsigned short protocol, IRadDbgComChannel** ppChannel,
                                   radMemoryAllocator alloc )
{
    *ppChannel = NULL;
}

void radDbgComMutexLock( )
{
    pthread_mutex_lock( &s_DbgComMutex );
}

void radDbgComMutexUnlock( )
{
    pthread_mutex_unlock( &s_DbgComMutex );
}
