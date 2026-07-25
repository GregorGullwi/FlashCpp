#include "ParserProfilingReport.h"
#include "Parser.h"
#include "TemplateProfilingStats.h"
#include "Log.h"
#include <iomanip>

namespace FlashCpp {

namespace {

void printBucketRow(const char* label, double bucket_ms, double parsing_time_ms, size_t count) {
	const double pct = parsing_time_ms > 0.0 ? (bucket_ms * 100.0 / parsing_time_ms) : 0.0;
	FLASH_LOG(General, Info, "  ", label,
			  ": count=", count,
			  ", total=", std::fixed, std::setprecision(3), bucket_ms, " ms",
			  " (", std::setprecision(2), pct, "% of Parsing)");
}

} // namespace

void printParsingPhaseBreakdown(const Parser* parser, double parsing_time_ms) {
#if !WITH_PARSER_RUNTIME_STATS
	(void)parser;
#endif
	FLASH_LOG(General, Info, "\n=== Parsing Phase Breakdown (", std::fixed, std::setprecision(3),
			  parsing_time_ms, " ms Parsing total) ===");

#if ENABLE_TEMPLATE_PROFILING
	const TemplateProfilingStats& template_stats = TemplateProfilingStats::getInstance();
	const double lookup_ms = template_stats.lookupTime().total_duration() / 1000.0;
	const double template_parsing_ms = template_stats.templateParsingTime().total_duration() / 1000.0;
	const double substitution_ms = template_stats.substitutionTime().total_duration() / 1000.0;
	const double specialization_ms = template_stats.specializationMatchTime().total_duration() / 1000.0;
	const double instantiation_ms = template_stats.totalInstantiationDurationUs() / 1000.0;
	const double instrumented_template_ms =
		lookup_ms + template_parsing_ms + substitution_ms + specialization_ms + instantiation_ms;

	FLASH_LOG(General, Info, "Template-engine buckets (PROFILE_TEMPLATE_* instrumentation):");
	printBucketRow("template instantiation", instantiation_ms, parsing_time_ms,
				   template_stats.totalInstantiationEventCount());
	printBucketRow("template lookup", lookup_ms, parsing_time_ms, template_stats.lookupTime().count());
	printBucketRow("template parsing", template_parsing_ms, parsing_time_ms,
				   template_stats.templateParsingTime().count());
	printBucketRow("type substitution", substitution_ms, parsing_time_ms,
				   template_stats.substitutionTime().count());
	printBucketRow("specialization matching", specialization_ms, parsing_time_ms,
				   template_stats.specializationMatchTime().count());
	FLASH_LOG(General, Info, "  instrumented template total: ", std::fixed, std::setprecision(3),
			  instrumented_template_ms, " ms",
			  " (", std::setprecision(2),
			  (parsing_time_ms > 0.0 ? instrumented_template_ms * 100.0 / parsing_time_ms : 0.0),
			  "% of Parsing)");
	FLASH_LOG(General, Info, "  note: template buckets are sparsely instrumented today; uninstrumented template work appears in parser phases below");
#else
	FLASH_LOG(General, Info, "Template-engine buckets: unavailable (ENABLE_TEMPLATE_PROFILING=0)");
#endif

#if WITH_PARSER_RUNTIME_STATS
	if (parser == nullptr || !parser->runtimeStatsEnabled()) {
		FLASH_LOG(General, Info, "Parser runtime phases: unavailable (rebuild with WITH_PARSER_RUNTIME_STATS=1 and pass --perf-stats)");
		return;
	}

	const Parser::RuntimeStats& stats = parser->runtimeStats();
	const double save_restore_ms =
		(stats.save_time_us + stats.restore_time_us + stats.restore_lexer_only_time_us +
		 stats.discard_time_us) /
		1000.0;

	FLASH_LOG(General, Info, "Parser lexer/token hot path:");
	printBucketRow("saved-token save", stats.save_time_us / 1000.0, parsing_time_ms, stats.save_count);
	printBucketRow("saved-token restore (full)", stats.restore_time_us / 1000.0, parsing_time_ms,
				   stats.restore_count);
	printBucketRow("saved-token restore (lexer-only)", stats.restore_lexer_only_time_us / 1000.0,
				   parsing_time_ms, stats.restore_lexer_only_count);
	printBucketRow("saved-token discard", stats.discard_time_us / 1000.0, parsing_time_ms,
				   stats.discard_count);
	FLASH_LOG(General, Info, "  saved-token operations total: ", std::fixed, std::setprecision(3),
			  save_restore_ms, " ms",
			  " (", std::setprecision(2),
			  (parsing_time_ms > 0.0 ? save_restore_ms * 100.0 / parsing_time_ms : 0.0),
			  "% of Parsing)");

	double total_self_ms = 0.0;
	for (size_t i = 0; i < static_cast<size_t>(Parser::RuntimePhase::Count); ++i) {
		total_self_ms += stats.phase_stats[i].self_time_us / 1000.0;
	}

	FLASH_LOG(General, Info, "Parser runtime phases (self time, nested phases excluded):");
	for (size_t i = 0; i < static_cast<size_t>(Parser::RuntimePhase::Count); ++i) {
		const auto phase = static_cast<Parser::RuntimePhase>(i);
		const Parser::RuntimePhaseStat& phase_stat = stats.phase_stats[i];
		if (phase_stat.calls == 0) {
			continue;
		}
		const double self_ms = phase_stat.self_time_us / 1000.0;
		const double inclusive_ms = phase_stat.inclusive_time_us / 1000.0;
		const double self_pct = parsing_time_ms > 0.0 ? self_ms * 100.0 / parsing_time_ms : 0.0;
		const double inclusive_pct =
			parsing_time_ms > 0.0 ? inclusive_ms * 100.0 / parsing_time_ms : 0.0;
		FLASH_LOG(General, Info, "  ", Parser::runtimePhaseName(phase),
				  ": calls=", phase_stat.calls,
				  ", self=", std::fixed, std::setprecision(3), self_ms, " ms (", std::setprecision(2),
				  self_pct, "%)",
				  ", inclusive=", std::setprecision(3), inclusive_ms, " ms (", inclusive_pct, "%)",
				  ", saves=", phase_stat.saves,
				  ", restores=", phase_stat.restores,
				  ", discards=", phase_stat.discards,
				  ", ast-allocs=", phase_stat.ast_nodes_allocated);
	}

	const double unaccounted_ms = parsing_time_ms - total_self_ms - save_restore_ms;
	FLASH_LOG(General, Info, "  parser phase self total: ", std::fixed, std::setprecision(3), total_self_ms,
			  " ms");
	FLASH_LOG(General, Info, "  parsing unattributed remainder: ", std::setprecision(3), unaccounted_ms,
			  " ms",
			  " (", std::setprecision(2),
			  (parsing_time_ms > 0.0 ? unaccounted_ms * 100.0 / parsing_time_ms : 0.0), "%)");
#else
	FLASH_LOG(General, Info, "Parser runtime phases: unavailable (rebuild with WITH_PARSER_RUNTIME_STATS=1 and pass --perf-stats)");
#endif
}

} // namespace FlashCpp
