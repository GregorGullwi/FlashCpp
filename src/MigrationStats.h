#pragma once

#include <cstdint>
#include "Log.h"

// Always-available migration telemetry for front-end rearchitecture boundary 0/4.
// Directional evidence for legacy-path burn-down; never compile-time gated.
// Each counter has a fixed corpus baseline in tests/migration_counters/.

inline uint64_t gTokenReplayCount = 0;
inline uint64_t gPostParseParserTypingQueryCount = 0;
inline uint64_t gAstToIrSemanticQueryCount = 0;
inline uint64_t gCodegenToParserCallbackCount = 0;
inline uint64_t gTemplateEngineOldEngineRouteCount = 0;
inline uint64_t gDollarIdentityRecoveryCount = 0;

inline void recordTokenReplay() {
	++gTokenReplayCount;
}

inline void recordPostParseParserTypingQuery() {
	++gPostParseParserTypingQueryCount;
}

inline void recordAstToIrSemanticQuery() {
	++gAstToIrSemanticQueryCount;
}

inline void recordCodegenToParserCallback() {
	++gCodegenToParserCallbackCount;
}

inline void recordTemplateEngineOldEngineRoute() {
	++gTemplateEngineOldEngineRouteCount;
}

inline void recordDollarIdentityRecovery() {
	++gDollarIdentityRecoveryCount;
}

inline uint64_t tokenReplayCount() {
	return gTokenReplayCount;
}

inline uint64_t postParseParserTypingQueryCount() {
	return gPostParseParserTypingQueryCount;
}

inline uint64_t astToIrSemanticQueryCount() {
	return gAstToIrSemanticQueryCount;
}

inline uint64_t codegenToParserCallbackCount() {
	return gCodegenToParserCallbackCount;
}

inline uint64_t templateEngineOldEngineRouteCount() {
	return gTemplateEngineOldEngineRouteCount;
}

inline uint64_t dollarIdentityRecoveryCount() {
	return gDollarIdentityRecoveryCount;
}

inline void printMigrationTelemetry() {
	FLASH_LOG(General, Info, "\nToken replays: ", gTokenReplayCount);
	FLASH_LOG(General, Info, "Post-parse parser typing queries: ", gPostParseParserTypingQueryCount);
	FLASH_LOG(General, Info, "AST-to-IR semantic queries: ", gAstToIrSemanticQueryCount);
	FLASH_LOG(General, Info, "Codegen-to-parser callbacks: ", gCodegenToParserCallbackCount);
	FLASH_LOG(General, Info, "TemplateEngine old-engine routes: ", gTemplateEngineOldEngineRouteCount);
	FLASH_LOG(General, Info, "Dollar identity recoveries: ", gDollarIdentityRecoveryCount);
}
