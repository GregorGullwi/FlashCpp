#pragma once

// Small, dependency-free benchmark runner for the parallel front-end
// experiments.  This header is intentionally outside the shipping projects.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace flash::parallel_frontend_benchmark {

struct BenchmarkConfig {
	std::string benchmark = "unnamed";
	std::string variant = "default";
	std::string input;
	std::string output = "-";
	std::uint64_t seed = 0x9e3779b97f4a7c15ULL;
	std::size_t warmup_iterations = 2;
	std::size_t iterations = 10;
	std::size_t workers = 1;
	std::map<std::string, std::string, std::less<>> extra;
};

// Return a value which is independent of scheduling and can be checked by the
// runner on every measured iteration.  A benchmark should hash its observable
// result into result_hash, rather than using an address or an iteration count.
struct BenchmarkResult {
	std::uint64_t result_hash = 0;
	std::uint64_t operations = 0;
	std::uint64_t bytes = 0;
};

struct BenchmarkContext {
	BenchmarkContext(const BenchmarkConfig& config_value, std::size_t iteration_value, bool warmup_value,
		std::uint64_t seed_value) noexcept
		: config(config_value), iteration(iteration_value), warmup(warmup_value), seed(seed_value),
		  random_state_(seed_value) {}

	const BenchmarkConfig& config;
	std::size_t iteration = 0;
	bool warmup = false;
	std::uint64_t seed = 0;

	// Deterministic, allocation-free pseudo-random stream for schedule and
	// workload perturbations.  Do not use a process-global random generator.
	std::uint64_t nextRandom() noexcept;

private:
	std::uint64_t random_state_ = 0;
	friend int runBenchmark(const BenchmarkConfig&, const std::function<BenchmarkResult(BenchmarkContext&)>&,
		std::ostream&);
};

using BenchmarkFunction = std::function<BenchmarkResult(BenchmarkContext&)>;

// Parse the common command line.  Options are deliberately explicit so a
// typo cannot silently change a benchmark.  --config key=value can be used by
// a benchmark-specific driver and is included in the metadata record.
bool parseArguments(int argc, const char* const* argv, BenchmarkConfig& config, std::string& error,
	std::ostream& help_stream);

// Emit metadata, warmups, measured samples, and one summary as JSONL.  The
// first measured result hash is used as the expected hash for later samples;
// a mismatch is reported and causes a non-zero return value.
int runBenchmark(const BenchmarkConfig& config, const BenchmarkFunction& benchmark, std::ostream& output);

// Convenience entry point for a standalone benchmark executable.
int runBenchmark(int argc, const char* const* argv, const BenchmarkFunction& benchmark);

} // namespace flash::parallel_frontend_benchmark
