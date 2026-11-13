// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* macOS has no <malloc.h>: what the sources expect from it. Only on the include path for APPLE builds. */
#ifndef SHAR_COMPAT_MACOS_MALLOC_H
#define SHAR_COMPAT_MACOS_MALLOC_H
#include <alloca.h>
#include <stdlib.h>
#include <malloc/malloc.h>
#endif
