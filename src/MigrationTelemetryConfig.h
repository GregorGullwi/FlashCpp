#pragma once

// Compile-time gates for internal migration and perf telemetry. Each macro defaults
// to 1 so current CI and local builds keep today's behavior. Release or
// shipping builds can disable individual families with =0 on the command line.

#ifndef FLASHCPP_TRACK_MIGRATION_COUNTERS
#define FLASHCPP_TRACK_MIGRATION_COUNTERS 1
#endif

#ifndef FLASHCPP_TRACK_INLINE_VECTOR_SPILLS
#define FLASHCPP_TRACK_INLINE_VECTOR_SPILLS 1
#endif

#ifndef FLASHCPP_TRACK_STACK_STRING_STATS
#define FLASHCPP_TRACK_STACK_STRING_STATS 1
#endif

#ifndef FLASHCPP_TRACK_OUTSIDE_ENGINE_DIAGNOSTICS
#define FLASHCPP_TRACK_OUTSIDE_ENGINE_DIAGNOSTICS 1
#endif

#if FLASHCPP_TRACK_MIGRATION_COUNTERS
#define FLASHCPP_MIGRATION_COUNTER_BODY(expr) expr
#else
#define FLASHCPP_MIGRATION_COUNTER_BODY(expr)
#endif

#if FLASHCPP_TRACK_STACK_STRING_STATS
#define FLASHCPP_STACK_STRING_STAT_BODY(expr) expr
#else
#define FLASHCPP_STACK_STRING_STAT_BODY(expr)
#endif

#if FLASHCPP_TRACK_OUTSIDE_ENGINE_DIAGNOSTICS
#define FLASHCPP_OUTSIDE_ENGINE_DIAGNOSTIC_BODY(expr) expr
#else
#define FLASHCPP_OUTSIDE_ENGINE_DIAGNOSTIC_BODY(expr)
#endif
