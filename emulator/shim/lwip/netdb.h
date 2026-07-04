/**
 * \file lwip/netdb.h (host shim) - system resolver.
 */
#pragma once

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#endif
