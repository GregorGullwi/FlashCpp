#include "benchmark_harness.h"

#include <cstdint>

using namespace flash::parallel_frontend_benchmark;

int main(int argc, const char* const* argv) {
	return runBenchmark(argc, argv, [](BenchmarkContext& context) {
		std::uint64_t hash = 0xcbf29ce484222325ULL;
		for (std::size_t index = 0; index < 10000; ++index) {
		// The smoke workload consumes the deterministic stream to exercise the
		// API, but hashes only the stable semantic result.
		(void)context.nextRandom();
		hash ^= static_cast<std::uint64_t>(index) + context.config.seed;
			hash *= 0x100000001b3ULL;
		}
		return BenchmarkResult{hash, 10000, 0};
	});
}
