#pragma once

#include <cstdint>

#include "InlineVector.h"
#include "Log.h"
#include "MigrationTelemetryConfig.h"

// Migration choke-point counters for front-end rearchitecture boundary 0/4.
// Disabled when FLASHCPP_TRACK_MIGRATION_COUNTERS=0. Each counter has a fixed
// corpus baseline in tests/migration_counters/.

inline uint64_t gTokenReplayCount = 0;
inline uint64_t gPostParseParserTypingQueryCount = 0;
inline uint64_t gAstToIrSemanticQueryCount = 0;
inline uint64_t gCodegenToParserCallbackCount = 0;
inline uint64_t gTemplateEngineOldEngineRouteCount = 0;
inline uint64_t gDollarIdentityRecoveryCount = 0;
inline uint64_t gDeclarationBuilderPublishCount = 0;

inline void recordTokenReplay() {
	FLASHCPP_MIGRATION_COUNTER_BODY(++gTokenReplayCount);
}

inline void recordPostParseParserTypingQuery() {
	FLASHCPP_MIGRATION_COUNTER_BODY(++gPostParseParserTypingQueryCount);
}

inline void recordAstToIrSemanticQuery() {
	FLASHCPP_MIGRATION_COUNTER_BODY(++gAstToIrSemanticQueryCount);
}

inline void recordCodegenToParserCallback() {
	FLASHCPP_MIGRATION_COUNTER_BODY(++gCodegenToParserCallbackCount);
}

inline void recordTemplateEngineOldEngineRoute() {
	FLASHCPP_MIGRATION_COUNTER_BODY(++gTemplateEngineOldEngineRouteCount);
}

inline void recordDollarIdentityRecovery() {
	FLASHCPP_MIGRATION_COUNTER_BODY(++gDollarIdentityRecoveryCount);
}

inline void recordDeclarationBuilderPublish() {
	FLASHCPP_MIGRATION_COUNTER_BODY(++gDeclarationBuilderPublishCount);
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

inline uint64_t declarationBuilderPublishCount() {
	return gDeclarationBuilderPublishCount;
}

inline void printMigrationTelemetry() {
	FLASH_LOG(General, Info, "\nToken replays: ", gTokenReplayCount);
	FLASH_LOG(General, Info, "Post-parse parser typing queries: ", gPostParseParserTypingQueryCount);
	FLASH_LOG(General, Info, "AST-to-IR semantic queries: ", gAstToIrSemanticQueryCount);
	FLASH_LOG(General, Info, "Codegen-to-parser callbacks: ", gCodegenToParserCallbackCount);
	FLASH_LOG(General, Info, "TemplateEngine old-engine routes: ", gTemplateEngineOldEngineRouteCount);
	FLASH_LOG(General, Info, "Dollar identity recoveries: ", gDollarIdentityRecoveryCount);
	FLASH_LOG(General, Info, "DeclarationBuilder publishes: ", gDeclarationBuilderPublishCount);
	FLASH_LOG(General, Info, "InlineVector spill events: ", FlashCpp::inlineVectorSpillCount());
	for (std::size_t index = 0; index < static_cast<std::size_t>(FlashCpp::InlineVectorSpillFamily::Count); ++index) {
		const uint64_t count =
			FlashCpp::inlineVectorSpillCount(static_cast<FlashCpp::InlineVectorSpillFamily>(index));
		if (count == 0) {
			continue;
		}
		FLASH_LOG(General, Info,
				  "InlineVector spill family ",
				  FlashCpp::inlineVectorSpillFamilyLabel(
					  static_cast<FlashCpp::InlineVectorSpillFamily>(index)),
				  " events: ",
				  count);
	}
}
