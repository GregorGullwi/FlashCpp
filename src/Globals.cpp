// Globals.cpp - Shared global state for FlashCpp compiler
// These globals are needed by both main.cpp and FlashCppTest.cpp.
// Included via FlashCppUnity.h (unity build).

#include "NamespaceRegistry.h"
#include "LazyMemberResolver.h"
#include "InstantiationQueue.h"

// Global debug flag
bool g_enable_debug_output = false;

// Global exception handling control
bool g_enable_exceptions = true;

NamespaceRegistry gNamespaceRegistry;
ChunkedAnyVector<> gChunkedAnyStorage;

namespace FlashCpp {
LazyMemberResolver gLazyMemberResolver;
InstantiationQueue gInstantiationQueue;
} // namespace FlashCpp
