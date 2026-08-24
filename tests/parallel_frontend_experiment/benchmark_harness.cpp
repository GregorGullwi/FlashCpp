#include "benchmark_harness.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#elif defined(__linux__)
#include <sys/resource.h>
#include <time.h>
#endif

namespace flash::parallel_frontend_benchmark {
namespace {

struct ProcessMetrics {
	std::uint64_t cpu_ns = 0;
	std::uint64_t peak_rss_bytes = 0;
	bool cpu_supported = false;
	bool rss_supported = false;
};

std::uint64_t processCpuNanoseconds(bool& supported) noexcept {
#if defined(_WIN32)
	FILETIME creation{};
	FILETIME exit{};
	FILETIME kernel{};
	FILETIME user{};
	if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
		supported = false;
		return 0;
	}
	ULARGE_INTEGER kernel_value{};
	ULARGE_INTEGER user_value{};
	kernel_value.LowPart = kernel.dwLowDateTime;
	kernel_value.HighPart = kernel.dwHighDateTime;
	user_value.LowPart = user.dwLowDateTime;
	user_value.HighPart = user.dwHighDateTime;
	supported = true;
	return (kernel_value.QuadPart + user_value.QuadPart) * 100ULL;
#elif defined(_POSIX_C_SOURCE) || defined(__linux__) || defined(__APPLE__)
	timespec value{};
	if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value) != 0) {
		supported = false;
		return 0;
	}
	supported = true;
	return static_cast<std::uint64_t>(value.tv_sec) * 1000000000ULL +
		static_cast<std::uint64_t>(value.tv_nsec);
#else
	supported = false;
	return 0;
#endif
}

std::uint64_t peakResidentBytes(bool& supported) noexcept {
#if defined(_WIN32)
	PROCESS_MEMORY_COUNTERS counters{};
	if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
		supported = false;
		return 0;
	}
	supported = true;
	return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
#elif defined(__linux__)
	rusage usage{};
	if (getrusage(RUSAGE_SELF, &usage) != 0) {
		supported = false;
		return 0;
	}
	supported = true;
	return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ULL;
#elif defined(__APPLE__)
	rusage usage{};
	if (getrusage(RUSAGE_SELF, &usage) != 0) {
		supported = false;
		return 0;
	}
	supported = true;
	return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
	supported = false;
	return 0;
#endif
}

std::string jsonEscape(std::string_view value) {
	std::string escaped;
	escaped.reserve(value.size() + 2);
	for (const unsigned char character : value) {
		switch (character) {
		case '"': escaped += "\\\""; break;
		case '\\': escaped += "\\\\"; break;
		case '\b': escaped += "\\b"; break;
		case '\f': escaped += "\\f"; break;
		case '\n': escaped += "\\n"; break;
		case '\r': escaped += "\\r"; break;
		case '\t': escaped += "\\t"; break;
		default:
			if (character < 0x20) {
				char buffer[7]{};
				std::snprintf(buffer, sizeof(buffer), "\\u%04x", character);
				escaped += buffer;
			} else {
				escaped += static_cast<char>(character);
			}
			break;
		}
	}
	return escaped;
}

void writeJsonString(std::ostream& output, std::string_view value) {
	output << '"' << jsonEscape(value) << '"';
}

bool parseUnsigned(std::string_view text, std::uint64_t& value) {
	if (text.empty())
		return false;
	const char* begin = text.data();
	const char* end = begin + text.size();
	int base = 10;
	if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
		begin += 2;
		base = 16;
	}
	const auto result = std::from_chars(begin, end, value, base);
	return result.ec == std::errc{} && result.ptr == end;
}

bool takeOptionValue(int& index, int argc, const char* const* argv, std::string_view option,
	std::string_view& value, std::string& error) {
	if (index + 1 >= argc) {
		error = "missing value for " + std::string(option);
		return false;
	}
	value = argv[++index];
	if (value.empty()) {
		error = "empty value for " + std::string(option);
		return false;
	}
	return true;
}

std::string compilerFamily() {
#if defined(__clang__)
	return "clang";
#elif defined(_MSC_VER)
	return "msvc";
#elif defined(__GNUC__)
	return "gcc";
#else
	return "unknown";
#endif
}

std::string compilerVersion() {
#if defined(__clang__)
	return __clang_version__;
#elif defined(_MSC_VER)
	return std::to_string(_MSC_VER);
#elif defined(__GNUC__)
	return std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." +
		std::to_string(__GNUC_PATCHLEVEL__);
#else
	return "unknown";
#endif
}

std::string architecture() {
#if defined(_M_X64) || defined(__x86_64__)
	return "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
	return "arm64";
#elif defined(_M_IX86) || defined(__i386__)
	return "x86";
#elif defined(_M_ARM) || defined(__arm__)
	return "arm";
#else
	return "unknown";
#endif
}

std::string operatingSystem() {
#if defined(_WIN32)
	return "Windows";
#elif defined(__linux__)
	return "Linux";
#elif defined(__APPLE__)
	return "macOS";
#else
	return "unknown";
#endif
}

std::string cpuModel() {
#if defined(_WIN32)
	char identifier[256]{};
	const DWORD length = GetEnvironmentVariableA("PROCESSOR_IDENTIFIER", identifier, sizeof(identifier));
	if (length != 0 && length < sizeof(identifier))
		return identifier;
#elif defined(__linux__)
	std::ifstream cpu_info("/proc/cpuinfo");
	std::string line;
	while (std::getline(cpu_info, line)) {
		constexpr std::string_view prefix = "model name\t: ";
		if (line.starts_with(prefix))
			return line.substr(prefix.size());
		constexpr std::string_view alternate_prefix = "Hardware\t: ";
		if (line.starts_with(alternate_prefix))
			return line.substr(alternate_prefix.size());
	}
#endif
	return architecture();
}

std::uint64_t splitMix64(std::uint64_t value) noexcept {
	value += 0x9e3779b97f4a7c15ULL;
	value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
	value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
	return value ^ (value >> 31);
}

struct Sample {
	std::size_t iteration = 0;
	std::uint64_t seed = 0;
	std::uint64_t wall_ns = 0;
	std::uint64_t cpu_ns = 0;
	std::uint64_t peak_rss_bytes = 0;
	BenchmarkResult result;
};

std::uint64_t percentile(const std::vector<std::uint64_t>& values, double fraction) {
	if (values.empty())
		return 0;
	const double position = fraction * static_cast<double>(values.size() - 1);
	const std::size_t lower = static_cast<std::size_t>(position);
	const std::size_t upper = std::min(lower + 1, values.size() - 1);
	const double interpolated = static_cast<double>(values[lower]) +
		(static_cast<double>(values[upper] - values[lower]) * (position - static_cast<double>(lower)));
	return static_cast<std::uint64_t>(interpolated + 0.5);
}

struct Statistics {
	std::uint64_t median = 0;
	std::uint64_t p95 = 0;
	std::uint64_t iqr = 0;
	std::uint64_t mad = 0;
};

Statistics summarize(std::vector<std::uint64_t> values) {
	std::sort(values.begin(), values.end());
	Statistics result;
	result.median = percentile(values, 0.50);
	result.p95 = percentile(values, 0.95);
	const std::uint64_t q1 = percentile(values, 0.25);
	const std::uint64_t q3 = percentile(values, 0.75);
	result.iqr = q3 >= q1 ? q3 - q1 : 0;
	std::vector<std::uint64_t> deviations;
	deviations.reserve(values.size());
	for (const std::uint64_t value : values)
		deviations.push_back(value >= result.median ? value - result.median : result.median - value);
	std::sort(deviations.begin(), deviations.end());
	result.mad = percentile(deviations, 0.50);
	return result;
}

void writeCommonMetadata(std::ostream& output, const BenchmarkConfig& config) {
	bool cpu_supported = false;
	bool rss_supported = false;
	(void)processCpuNanoseconds(cpu_supported);
	(void)peakResidentBytes(rss_supported);
	output << "{\"record\":\"metadata\",\"schema\":1,\"machine\":{";
	output << "\"os\":";
	writeJsonString(output, operatingSystem());
	output << ",\"architecture\":";
	writeJsonString(output, architecture());
	output << ",\"cpu_model\":";
	writeJsonString(output, cpuModel());
	output << ",\"logical_cpus\":" << std::thread::hardware_concurrency();
	output << ",\"cpu_time_supported\":" << (cpu_supported ? "true" : "false");
	output << ",\"peak_rss_supported\":" << (rss_supported ? "true" : "false") << "},\"compiler\":{";
	output << "\"family\":";
	writeJsonString(output, compilerFamily());
	output << ",\"version\":";
	writeJsonString(output, compilerVersion());
	output << ",\"language\":\"C++20\"},\"config\":{";
	output << "\"benchmark\":";
	writeJsonString(output, config.benchmark);
	output << ",\"variant\":";
	writeJsonString(output, config.variant);
	output << ",\"input\":";
	writeJsonString(output, config.input);
	output << ",\"workers\":" << config.workers;
	output << ",\"seed\":" << config.seed;
	output << ",\"warmup_iterations\":" << config.warmup_iterations;
	output << ",\"iterations\":" << config.iterations;
	for (const auto& [key, value] : config.extra) {
		output << ',';
		writeJsonString(output, key);
		output << ':';
		writeJsonString(output, value);
	}
	output << "}}\n";
}

void writeError(std::ostream& output, std::string_view message) {
	output << "{\"record\":\"error\",\"message\":";
	writeJsonString(output, message);
	output << "}\n";
}

} // namespace

std::uint64_t BenchmarkContext::nextRandom() noexcept {
	random_state_ = splitMix64(random_state_);
	return random_state_;
}

bool parseArguments(int argc, const char* const* argv, BenchmarkConfig& config, std::string& error,
	std::ostream& help_stream) {
	for (int index = 1; index < argc; ++index) {
		const std::string_view argument = argv[index];
		if (argument == "--help" || argument == "-h") {
			help_stream << "Usage: benchmark [options]\n"
				"  --benchmark NAME       workload name (default: unnamed)\n"
				"  --variant NAME         implementation/configuration label\n"
				"  --input PATH           workload or corpus label\n"
				"  --workers N            fixed worker budget (default: 1)\n"
				"  --seed N               deterministic decimal or 0x-prefixed seed\n"
				"  --warmup N             warmup iterations (default: 2)\n"
				"  --iterations N         measured iterations, at least 10 (default: 10)\n"
				"  --output PATH          JSONL output, or - for stdout\n"
				"  --config KEY=VALUE     extra metadata (repeatable)\n";
			return false;
		}
		std::string_view value;
		if (argument == "--benchmark" || argument == "--variant" || argument == "--input" ||
			argument == "--output") {
			if (!takeOptionValue(index, argc, argv, argument, value, error))
				return false;
			std::string* destination = argument == "--benchmark" ? &config.benchmark :
				argument == "--variant" ? &config.variant : argument == "--input" ? &config.input : &config.output;
			*destination = value;
			continue;
		}
		if (argument == "--workers" || argument == "--seed" || argument == "--warmup" || argument == "--iterations") {
			if (!takeOptionValue(index, argc, argv, argument, value, error))
				return false;
			std::uint64_t parsed = 0;
			if (!parseUnsigned(value, parsed)) {
				error = "invalid unsigned value for " + std::string(argument) + ": " + std::string(value);
				return false;
			}
			if (argument == "--workers") config.workers = static_cast<std::size_t>(parsed);
			else if (argument == "--seed") config.seed = parsed;
			else if (argument == "--warmup") config.warmup_iterations = static_cast<std::size_t>(parsed);
			else config.iterations = static_cast<std::size_t>(parsed);
			continue;
		}
		if (argument == "--config") {
			if (!takeOptionValue(index, argc, argv, argument, value, error))
				return false;
			const std::size_t equals = value.find('=');
			if (equals == std::string_view::npos || equals == 0) {
				error = "--config expects KEY=VALUE";
				return false;
			}
			config.extra[std::string(value.substr(0, equals))] = std::string(value.substr(equals + 1));
			continue;
		}
		error = "unknown option: " + std::string(argument);
		return false;
	}
	if (config.workers == 0) {
		error = "--workers must be at least 1";
		return false;
	}
	if (config.iterations < 10) {
		error = "--iterations must be at least 10";
		return false;
	}
	return true;
}

int runBenchmark(const BenchmarkConfig& config, const BenchmarkFunction& benchmark, std::ostream& output) {
	if (!benchmark) {
		writeError(output, "benchmark callback is empty");
		return 2;
	}
	if (config.workers == 0 || config.iterations < 10) {
		writeError(output, config.workers == 0 ? "workers must be at least 1" : "iterations must be at least 10");
		return 2;
	}
	writeCommonMetadata(output, config);
	std::vector<Sample> samples;
	samples.reserve(config.iterations);
	std::uint64_t expected_hash = 0;
	bool has_expected_hash = false;
	bool hash_consistent = true;
	for (std::size_t index = 0; index < config.warmup_iterations + config.iterations; ++index) {
		const bool warmup = index < config.warmup_iterations;
		const std::size_t measurement_index = warmup ? index : index - config.warmup_iterations;
		BenchmarkContext context{config, measurement_index, warmup,
			splitMix64(config.seed ^ static_cast<std::uint64_t>(measurement_index))};
		const auto wall_start = std::chrono::steady_clock::now();
		bool cpu_start_supported = false;
		const std::uint64_t cpu_start = processCpuNanoseconds(cpu_start_supported);
		BenchmarkResult result;
		try {
			result = benchmark(context);
		} catch (const std::exception& exception) {
			writeError(output, exception.what());
			return 1;
		} catch (...) {
			writeError(output, "benchmark callback threw a non-standard exception");
			return 1;
		}
		const auto wall_end = std::chrono::steady_clock::now();
		bool cpu_end_supported = false;
		const std::uint64_t cpu_end = processCpuNanoseconds(cpu_end_supported);
		bool rss_supported = false;
		const std::uint64_t rss = peakResidentBytes(rss_supported);
		const std::uint64_t wall_ns = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(wall_end - wall_start).count());
		const std::uint64_t cpu_ns = cpu_start_supported && cpu_end_supported && cpu_end >= cpu_start ?
			cpu_end - cpu_start : 0;
		output << "{\"record\":\"sample\",\"phase\":";
		writeJsonString(output, warmup ? "warmup" : "measure");
		output << ",\"iteration\":" << measurement_index << ",\"seed\":" << context.seed;
		output << ",\"wall_ns\":" << wall_ns << ",\"cpu_ns\":" << cpu_ns;
		output << ",\"peak_rss_bytes\":" << rss << ",\"result_hash\":" << result.result_hash;
		output << ",\"operations\":" << result.operations << ",\"bytes\":" << result.bytes;
		output << ",\"metrics_supported\":{\"cpu_ns\":" << (cpu_start_supported && cpu_end_supported ? "true" : "false");
		output << ",\"peak_rss_bytes\":" << (rss_supported ? "true" : "false") << "}}\n";
		if (warmup)
			continue;
		if (!has_expected_hash) {
			expected_hash = result.result_hash;
			has_expected_hash = true;
		} else if (result.result_hash != expected_hash) {
			hash_consistent = false;
		}
		samples.push_back(Sample{measurement_index, context.seed, wall_ns, cpu_ns, rss, result});
	}
	std::vector<std::uint64_t> wall_values;
	std::vector<std::uint64_t> cpu_values;
	wall_values.reserve(samples.size());
	cpu_values.reserve(samples.size());
	for (const Sample& sample : samples) {
		wall_values.push_back(sample.wall_ns);
		cpu_values.push_back(sample.cpu_ns);
	}
	const Statistics wall_stats = summarize(wall_values);
	const Statistics cpu_stats = summarize(cpu_values);
	output << "{\"record\":\"summary\",\"iterations\":" << samples.size();
	output << ",\"result_hash\":" << expected_hash << ",\"result_hash_consistent\":"
		<< (hash_consistent ? "true" : "false");
	output << ",\"wall_ns\":{\"median\":" << wall_stats.median << ",\"p95\":" << wall_stats.p95
		<< ",\"iqr\":" << wall_stats.iqr << ",\"mad\":" << wall_stats.mad << "}";
	output << ",\"cpu_ns\":{\"median\":" << cpu_stats.median << ",\"p95\":" << cpu_stats.p95
		<< ",\"iqr\":" << cpu_stats.iqr << ",\"mad\":" << cpu_stats.mad << "}}\n";
	return hash_consistent ? 0 : 1;
}

int runBenchmark(int argc, const char* const* argv, const BenchmarkFunction& benchmark) {
	BenchmarkConfig config;
	std::string error;
	std::ostringstream help;
	if (!parseArguments(argc, argv, config, error, help)) {
		if (!help.str().empty()) {
			std::cout << help.str();
			return 0;
		}
		std::cerr << "benchmark argument error: " << error << '\n';
		return 2;
	}
	if (config.output == "-")
		return runBenchmark(config, benchmark, std::cout);
	std::ofstream output(config.output, std::ios::binary | std::ios::trunc);
	if (!output) {
		std::cerr << "cannot open benchmark output: " << config.output << '\n';
		return 2;
	}
	return runBenchmark(config, benchmark, output);
}

} // namespace flash::parallel_frontend_benchmark
