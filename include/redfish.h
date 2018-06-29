//----------------------------------------------------------------------------
// Copyright Notice:
// Copyright 2017 Distributed Management Task Force, Inc. All rights reserved.
// License: BSD 3-Clause License. For full text see link: https://github.com/DMTF/libredfish/blob/master/LICENSE.md
//----------------------------------------------------------------------------
#ifndef _REDFISH_H_
#define _REDFISH_H_

#include <redfishService.h>
#include <redfishPayload.h>
#include <redpath.h>

/**
 * syslog style function used to debug libredfish.
 */
typedef void (*libRedfishDebugFunc)(int priority, const char* message, ...);

/**
 * Set the debug function to be used for libredfish. NOTE: This will do nothing unless
 * the _DEBUG macro is set during compilation time of the library.
 *
 * @param debugFunc The debug function to use, NULL disables debug.
 */
void libredfishSetDebugFunction(libRedfishDebugFunc debugFunc);

#endif