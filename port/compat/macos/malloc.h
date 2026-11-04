/* macOS has no <malloc.h>: what the sources expect from it. Only on the include path for APPLE builds. */
#ifndef SHAR_COMPAT_MACOS_MALLOC_H
#define SHAR_COMPAT_MACOS_MALLOC_H
#include <alloca.h>
#include <stdlib.h>
#include <malloc/malloc.h>
#endif
