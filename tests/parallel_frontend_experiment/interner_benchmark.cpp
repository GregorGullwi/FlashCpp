// Experiment boundary 3: canonical structural type interner comparison.
//
// This is deliberately standalone test code.  It does not use FlashCpp's
// production allocator or type system: the benchmark is intended to make the
// synchronization and publication costs visible before an architectural
// choice is made.

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

namespace {

using Clock = std::chrono::steady_clock;

enum class TypeKind : std::uint8_t {
	Builtin,
	Pointer,
	Reference,
	Array,
	Function,
	MemberPointer,
	Qualified,
	TemplateParameter,
	Specialization,
};

// Every field is an immutable structural component.  In particular, this
// benchmark never uses an insertion order or a worker-local numeric ID in a
// TypeKey.  Child types are represented by stable structural fingerprints.
struct TypeKey {
	TypeKind kind{};
	std::uint8_t qualifiers{};
	std::uint16_t flags{};
	std::uint64_t a{};
	std::uint64_t b{};
	std::uint64_t c{};
	std::uint64_t d{};

	friend bool operator==(const TypeKey&, const TypeKey&) = default;
};

static_assert(sizeof(TypeKey) <= 40, "keep the structural key compact");

std::uint64_t mix(std::uint64_t x) {
	x += 0x9e3779b97f4a7c15ULL;
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
	return x ^ (x >> 31);
}

std::uint64_t fingerprint(const TypeKey& key) {
	std::uint64_t value = mix(static_cast<std::uint64_t>(key.kind));
	value = mix(value ^ key.qualifiers);
	value = mix(value ^ key.flags);
	value = mix(value ^ key.a);
	value = mix(value ^ key.b);
	value = mix(value ^ key.c);
	return mix(value ^ key.d);
}

struct TypeKeyHash {
	std::size_t operator()(const TypeKey& key) const noexcept {
		return static_cast<std::size_t>(fingerprint(key));
	}
};

struct CanonicalType {
	const TypeKey key;
	const std::uint64_t structural_fingerprint;

	CanonicalType(const TypeKey& value, std::uint64_t hash)
		: key(value), structural_fingerprint(hash) {}
};

struct WorkerStats {
	std::uint64_t requests{};
	std::uint64_t hits{};
	std::uint64_t cache_hits{};
	std::uint64_t misses{};
	std::uint64_t duplicate_constructions{};
	std::uint64_t lock_wait_ns{};
	std::uint64_t arena_wait_ns{};
	std::uint64_t bytes_lost_to_races{};
};

struct ArenaChunk {
	static constexpr std::size_t Capacity = 256;
	alignas(CanonicalType) std::array<std::byte, Capacity * sizeof(CanonicalType)> storage{};
	std::size_t used{};
};

// Chunks are leased while holding this short-lived lock.  Once a worker owns
// a chunk, all common allocations are local and do not take an interner lock.
class ChunkLeasePool {
public:
	ArenaChunk* lease(WorkerStats& stats) {
		const auto before = Clock::now();
		std::lock_guard lock(mutex_);
		const auto after = Clock::now();
		stats.arena_wait_ns += static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(after - before).count());
		chunks_.push_back(std::make_unique<ArenaChunk>());
		return chunks_.back().get();
	}

	std::size_t reservedBytes() const {
		return chunks_.size() * sizeof(ArenaChunk);
	}

private:
	mutable std::mutex mutex_;
	std::vector<std::unique_ptr<ArenaChunk>> chunks_;
};

class WorkerArena {
public:
	WorkerArena(ChunkLeasePool& pool, WorkerStats& stats) : pool_(pool), stats_(stats) {}

	CanonicalType* make(const TypeKey& key) {
		if (chunk_ == nullptr || chunk_->used == ArenaChunk::Capacity)
			chunk_ = pool_.lease(stats_);
		std::byte* address = chunk_->storage.data() + chunk_->used * sizeof(CanonicalType);
		++chunk_->used;
		return new (address) CanonicalType(key, fingerprint(key));
	}

private:
	ChunkLeasePool& pool_;
	WorkerStats& stats_;
	ArenaChunk* chunk_{};
};

class Interner {
public:
	virtual ~Interner() = default;
	virtual CanonicalType* intern(const TypeKey&, WorkerArena&, WorkerStats&) = 0;
};

template <typename Map>
void reserveMap(Map& map, std::size_t key_count) {
	map.reserve(key_count);
}

class MutexInterner final : public Interner {
public:
	explicit MutexInterner(std::size_t expected_keys) { reserveMap(table_, expected_keys); }

	CanonicalType* intern(const TypeKey& key, WorkerArena& arena, WorkerStats& stats) override {
		{
			const auto before = Clock::now();
			std::lock_guard lock(mutex_);
			const auto after = Clock::now();
			stats.lock_wait_ns += elapsedNs(before, after);
			auto found = table_.find(key);
			if (found != table_.end()) {
				++stats.hits;
				return found->second;
			}
		}

		CanonicalType* candidate = arena.make(key);
		const auto before = Clock::now();
		std::lock_guard lock(mutex_);
		const auto after = Clock::now();
		stats.lock_wait_ns += elapsedNs(before, after);
		auto [found, inserted] = table_.emplace(key, candidate);
		if (inserted) {
			++stats.misses;
			return candidate;
		}
		++stats.hits;
		++stats.duplicate_constructions;
		stats.bytes_lost_to_races += sizeof(CanonicalType);
		return found->second;
	}

	std::size_t canonicalCount() const { return table_.size(); }

private:
	static std::uint64_t elapsedNs(Clock::time_point before, Clock::time_point after) {
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(after - before).count());
	}

	std::mutex mutex_;
	std::unordered_map<TypeKey, CanonicalType*, TypeKeyHash> table_;
};

class ShardedInterner final : public Interner {
public:
	static constexpr std::size_t ShardCount = 32;

	explicit ShardedInterner(std::size_t expected_keys) {
		for (auto& shard : shards_)
			reserveMap(shard.table, expected_keys / ShardCount + 8);
	}

	CanonicalType* intern(const TypeKey& key, WorkerArena& arena, WorkerStats& stats) override {
		Shard& shard = shards_[fingerprint(key) % ShardCount];
		{
			const auto before = Clock::now();
			std::lock_guard lock(shard.mutex);
			const auto after = Clock::now();
			stats.lock_wait_ns += elapsedNs(before, after);
			auto found = shard.table.find(key);
			if (found != shard.table.end()) {
				++stats.hits;
				return found->second;
			}
		}

		CanonicalType* candidate = arena.make(key);
		const auto before = Clock::now();
		std::lock_guard lock(shard.mutex);
		const auto after = Clock::now();
		stats.lock_wait_ns += elapsedNs(before, after);
		auto [found, inserted] = shard.table.emplace(key, candidate);
		if (inserted) {
			++stats.misses;
			return candidate;
		}
		++stats.hits;
		++stats.duplicate_constructions;
		stats.bytes_lost_to_races += sizeof(CanonicalType);
		return found->second;
	}

	std::size_t canonicalCount() const {
		std::size_t count = 0;
		for (const Shard& shard : shards_)
			count += shard.table.size();
		return count;
	}

private:
	struct Shard {
		std::mutex mutex;
		std::unordered_map<TypeKey, CanonicalType*, TypeKeyHash> table;
	};

	static std::uint64_t elapsedNs(Clock::time_point before, Clock::time_point after) {
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(after - before).count());
	}

	std::array<Shard, ShardCount> shards_;
};

class CachedSharedInterner final : public Interner {
public:
	explicit CachedSharedInterner(std::size_t expected_keys) { reserveMap(table_, expected_keys); }

	CanonicalType* intern(const TypeKey& key, WorkerArena& arena, WorkerStats& stats) override {
		const std::size_t slot = TypeKeyHash{}(key) % CacheSize;
		CacheEntry& entry = cache_[slot];
		if (entry.value != nullptr && entry.key == key) {
			++stats.hits;
			++stats.cache_hits;
			return entry.value;
		}

		CanonicalType* result = nullptr;
		{
			const auto before = Clock::now();
			std::lock_guard lock(mutex_);
			const auto after = Clock::now();
			stats.lock_wait_ns += elapsedNs(before, after);
			auto found = table_.find(key);
			if (found != table_.end()) {
				result = found->second;
				++stats.hits;
			}
		}
		if (result == nullptr) {
			// Construction and possible chunk leasing are deliberately outside
			// the table lock.  The second lookup publishes only a complete object.
			CanonicalType* candidate = arena.make(key);
			const auto before = Clock::now();
			std::lock_guard lock(mutex_);
			const auto after = Clock::now();
			stats.lock_wait_ns += elapsedNs(before, after);
			auto [stored, inserted] = table_.emplace(key, candidate);
			if (inserted) {
				result = candidate;
				++stats.misses;
			} else {
				result = stored->second;
				++stats.hits;
				++stats.duplicate_constructions;
				stats.bytes_lost_to_races += sizeof(CanonicalType);
			}
		}
		entry.key = key;
		entry.value = result;
		return result;
	}

	std::size_t canonicalCount() const { return table_.size(); }

	std::uint64_t cacheCapacity() const { return CacheSize; }

private:
	static constexpr std::size_t CacheSize = 64;
	struct CacheEntry {
		TypeKey key{};
		CanonicalType* value{};
	};

	static std::uint64_t elapsedNs(Clock::time_point before, Clock::time_point after) {
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(after - before).count());
	}

	std::mutex mutex_;
	std::unordered_map<TypeKey, CanonicalType*, TypeKeyHash> table_;
	// One cache per worker is installed by runBenchmark.  This object is
	// intentionally thread-local rather than protected by the shared mutex.
	static thread_local std::array<CacheEntry, CacheSize> cache_;
};

thread_local std::array<CachedSharedInterner::CacheEntry, CachedSharedInterner::CacheSize>
	CachedSharedInterner::cache_{};

struct Request {
	std::uint32_t key_index{};
	std::uint32_t sequence{};
};

struct Workload {
	std::vector<TypeKey> keys;
	std::vector<Request> requests;
};

std::uint64_t stableTypeAtom(std::uint64_t value) {
	return mix(value ^ 0x4f1bbcdcaa6d3e29ULL);
}

TypeKey key(TypeKind kind, std::uint64_t a, std::uint64_t b = 0,
	std::uint64_t c = 0, std::uint64_t d = 0, std::uint16_t flags = 0,
	std::uint8_t qualifiers = 0) {
	return TypeKey{kind, qualifiers, flags, a, b, c, d};
}

Workload makeWorkload(std::size_t key_count, std::size_t request_count, std::uint64_t seed) {
	Workload workload;
	workload.keys.reserve(key_count);
	const std::array<TypeKind, 9> kinds = {TypeKind::Builtin, TypeKind::Pointer,
		TypeKind::Reference, TypeKind::Array, TypeKind::Function,
		TypeKind::MemberPointer, TypeKind::Qualified, TypeKind::TemplateParameter,
		TypeKind::Specialization};

	for (std::size_t i = 0; i < key_count; ++i) {
		const TypeKind kind_value = kinds[i % kinds.size()];
		const std::uint64_t x = stableTypeAtom(i + 11);
		const std::uint64_t child = stableTypeAtom((i * 17) % (key_count / 2 + 1) + 1);
		const std::uint64_t sibling = stableTypeAtom((i * 31 + 7) % (key_count / 3 + 1) + 3);
		TypeKey value;
		switch (kind_value) {
		case TypeKind::Builtin:
			value = key(kind_value, i % 12, x, 0, 0, static_cast<std::uint16_t>(i % 4));
			break;
		case TypeKind::Pointer:
			value = key(kind_value, child, 0, 0, 0, 0, static_cast<std::uint8_t>(i % 8));
			break;
		case TypeKind::Reference:
			value = key(kind_value, child, i & 1, 0, 0);
			break;
		case TypeKind::Array:
			value = key(kind_value, child, 1 + (i % 32), x, 0);
			break;
		case TypeKind::Function:
			value = key(kind_value, child, sibling, stableTypeAtom(i % 7), 1 + (i % 6),
				static_cast<std::uint16_t>((i % 3) | ((i % 2) << 3)));
			break;
		case TypeKind::MemberPointer:
			value = key(kind_value, stableTypeAtom(1000 + i % 97), child, sibling, i % 5);
			break;
		case TypeKind::Qualified:
			value = key(kind_value, child, sibling, 0, 0, 0,
				static_cast<std::uint8_t>(1 + (i % 7)));
			break;
		case TypeKind::TemplateParameter:
			value = key(kind_value, stableTypeAtom(i % 31), i % 9, i % 2, 0);
			break;
		case TypeKind::Specialization:
			value = key(kind_value, stableTypeAtom(2000 + i % 113), child, sibling,
				(i % 4) + 1, static_cast<std::uint16_t>(i % 5));
			break;
		}
		workload.keys.push_back(value);
	}

	// A Zipf-like trace: the first 5% of keys receive most requests, while the
	// long tail still exercises every structural form and collision comparison.
	std::vector<std::uint64_t> weights(key_count);
	std::uint64_t total_weight = 0;
	for (std::size_t i = 0; i < key_count; ++i) {
		weights[i] = i < key_count / 20 + 1 ? 1000 - (i * 700 / (key_count / 20 + 1)) : 8;
		total_weight += weights[i];
	}
	std::mt19937_64 rng(seed);
	std::uniform_int_distribution<std::uint64_t> pick(0, total_weight - 1);
	workload.requests.reserve(request_count);
	for (std::size_t sequence = 0; sequence < request_count; ++sequence) {
		std::uint64_t ticket = pick(rng);
		std::size_t selected = 0;
		for (; selected + 1 < key_count && ticket >= weights[selected]; ++selected)
			ticket -= weights[selected];
		workload.requests.push_back(Request{static_cast<std::uint32_t>(selected),
			static_cast<std::uint32_t>(sequence)});
	}
	return workload;
}

enum class Variant { Mutex, Sharded, Cached };

std::string_view variantName(Variant variant) {
	switch (variant) {
	case Variant::Mutex: return "mutex";
	case Variant::Sharded: return "sharded";
	case Variant::Cached: return "cached_shared";
	}
	return "unknown";
}

struct RunResult {
	std::uint64_t elapsed_ns{};
	std::uint64_t requests{};
	std::uint64_t hits{};
	std::uint64_t cache_hits{};
	std::uint64_t misses{};
	std::uint64_t duplicates{};
	std::uint64_t lock_wait_ns{};
	std::uint64_t arena_wait_ns{};
	std::uint64_t bytes_lost{};
	std::uint64_t canonical_count{};
	std::uint64_t arena_reserved_bytes{};
	std::uint64_t output_hash{};
	std::uint64_t peak_rss_bytes{};
	bool stable{};
};

std::uint64_t elapsedNs(Clock::time_point before, Clock::time_point after) {
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(after - before).count());
}

std::uint64_t peakRssBytes() {
#ifdef _WIN32
	PROCESS_MEMORY_COUNTERS counters{};
	if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
		return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
	return 0;
#else
	std::ifstream status("/proc/self/status");
	std::string label;
	std::uint64_t value = 0;
	while (status >> label) {
		if (label == "VmHWM:") {
			status >> value;
			return value * 1024;
		}
		std::string rest;
		std::getline(status, rest);
	}
	return 0;
#endif
}

std::uint64_t hashResults(const std::vector<std::uint64_t>& results) {
	std::uint64_t hash = 0x6a09e667f3bcc909ULL;
	for (std::size_t i = 0; i < results.size(); ++i)
		hash = mix(hash ^ mix(results[i] + i * 0x9e3779b97f4a7c15ULL));
	return hash;
}

std::uint64_t expectedOutputHash(const Workload& workload) {
	std::vector<std::uint64_t> expected(workload.requests.size());
	for (const Request& request : workload.requests)
		expected[request.sequence] = fingerprint(workload.keys[request.key_index]);
	return hashResults(expected);
}

RunResult runBenchmark(const Workload& workload, Variant variant, std::size_t worker_count,
	std::uint64_t schedule_seed, bool publication_delay) {
	std::unique_ptr<Interner> interner;
	switch (variant) {
	case Variant::Mutex: interner = std::make_unique<MutexInterner>(workload.keys.size()); break;
	case Variant::Sharded: interner = std::make_unique<ShardedInterner>(workload.keys.size()); break;
	case Variant::Cached: interner = std::make_unique<CachedSharedInterner>(workload.keys.size()); break;
	}
	ChunkLeasePool pool;
	std::vector<WorkerStats> stats(worker_count);
	std::vector<std::uint64_t> results(workload.requests.size());
	std::vector<std::vector<Request>> schedules(worker_count);
	for (const Request& request : workload.requests)
		schedules[request.sequence % worker_count].push_back(request);
	for (std::size_t worker = 0; worker < worker_count; ++worker) {
		std::mt19937_64 rng(schedule_seed ^ mix(worker + 1));
		std::shuffle(schedules[worker].begin(), schedules[worker].end(), rng);
	}

	std::atomic<std::size_t> ready{0};
	std::atomic<bool> go{false};
	std::vector<std::thread> workers;
	workers.reserve(worker_count);
	for (std::size_t worker = 0; worker < worker_count; ++worker) {
		workers.emplace_back([&, worker] {
			WorkerStats& worker_stats = stats[worker];
			WorkerArena arena(pool, worker_stats);
			ready.fetch_add(1, std::memory_order_release);
			while (!go.load(std::memory_order_acquire))
				std::this_thread::yield();
			// Perturb start order without changing the request trace.
			for (std::uint64_t spin = 0; spin < ((schedule_seed + worker * 17) % 11); ++spin)
				std::this_thread::yield();
			for (const Request& request : schedules[worker]) {
				CanonicalType* canonical = interner->intern(workload.keys[request.key_index], arena, worker_stats);
				if (!(canonical->key == workload.keys[request.key_index]))
					std::abort();
				results[request.sequence] = canonical->structural_fingerprint;
				++worker_stats.requests;
				if (publication_delay && ((request.sequence + schedule_seed) % 257 == 0))
					std::this_thread::yield();
			}
		});
	}
	while (ready.load(std::memory_order_acquire) != worker_count)
		std::this_thread::yield();
	const auto started = Clock::now();
	go.store(true, std::memory_order_release);
	for (std::thread& worker : workers)
		worker.join();
	const auto finished = Clock::now();

	RunResult result;
	result.elapsed_ns = elapsedNs(started, finished);
	result.requests = workload.requests.size();
	for (const WorkerStats& worker : stats) {
		result.hits += worker.hits;
		result.cache_hits += worker.cache_hits;
		result.misses += worker.misses;
		result.duplicates += worker.duplicate_constructions;
		result.lock_wait_ns += worker.lock_wait_ns;
		result.arena_wait_ns += worker.arena_wait_ns;
		result.bytes_lost += worker.bytes_lost_to_races;
	}
	if (variant == Variant::Mutex)
		result.canonical_count = static_cast<MutexInterner*>(interner.get())->canonicalCount();
	else if (variant == Variant::Sharded)
		result.canonical_count = static_cast<ShardedInterner*>(interner.get())->canonicalCount();
	else
		result.canonical_count = static_cast<CachedSharedInterner*>(interner.get())->canonicalCount();
	result.arena_reserved_bytes = pool.reservedBytes();
	result.output_hash = hashResults(results);
	result.peak_rss_bytes = peakRssBytes();
	result.stable = result.canonical_count > 0 && result.misses == result.canonical_count &&
		result.output_hash == expectedOutputHash(workload);
	return result;
}

struct Options {
	std::size_t key_count = 4096;
	std::size_t request_count = 250000;
	std::size_t iterations = 10;
	std::size_t warmups = 2;
	std::vector<std::size_t> workers = {1, 2, 4, 8};
	std::uint64_t seed = 0x20260824;
	bool publication_delay = false;
};

std::size_t parseSize(std::string_view text, std::string_view option, bool allow_zero) {
	try {
		const std::size_t value = std::stoull(std::string(text));
		if (value == 0 && !allow_zero)
			throw std::invalid_argument("zero");
		return value;
	} catch (...) {
		throw std::runtime_error("invalid value for " + std::string(option));
	}
}

void printUsage() {
	std::cout << "interner_benchmark [--keys=N] [--requests=N] [--iterations=N] "
		"[--warmups=N] [--workers=1,2,4] [--seed=N] [--publication-delay]\n";
}

Options parseOptions(int argc, char** argv) {
	Options options;
	for (int i = 1; i < argc; ++i) {
		const std::string_view argument(argv[i]);
		if (argument == "--help" || argument == "-h") {
			printUsage();
			std::exit(0);
		}
		auto valueAfter = [&](std::string_view name) -> std::string_view {
			if (!argument.starts_with(name) || argument.size() <= name.size() || argument[name.size()] != '=')
				return {};
			return argument.substr(name.size() + 1);
		};
		if (auto keys_value = valueAfter("--keys"); !keys_value.empty()) options.key_count = parseSize(keys_value, "--keys", false);
		else if (auto requests_value = valueAfter("--requests"); !requests_value.empty()) options.request_count = parseSize(requests_value, "--requests", false);
		else if (auto iterations_value = valueAfter("--iterations"); !iterations_value.empty()) options.iterations = parseSize(iterations_value, "--iterations", true);
		else if (auto warmups_value = valueAfter("--warmups"); !warmups_value.empty()) options.warmups = parseSize(warmups_value, "--warmups", true);
		else if (auto seed_value = valueAfter("--seed"); !seed_value.empty()) options.seed = std::stoull(std::string(seed_value));
		else if (argument == "--publication-delay") options.publication_delay = true;
		else if (auto workers_value = valueAfter("--workers"); !workers_value.empty()) {
			options.workers.clear();
			std::stringstream stream{std::string(workers_value)};
			std::string item;
			while (std::getline(stream, item, ',')) options.workers.push_back(parseSize(item, "--workers", false));
			if (options.workers.empty()) throw std::runtime_error("--workers cannot be empty");
		} else {
			throw std::runtime_error("unknown option: " + std::string(argument));
		}
	}
	if (options.iterations < 10)
		throw std::runtime_error("the experiment requires at least 10 measured iterations");
	return options;
}

std::string cpuModel() {
#ifdef _WIN32
	char* model = nullptr;
	size_t model_size = 0;
	if (_dupenv_s(&model, &model_size, "PROCESSOR_IDENTIFIER") != 0 || model == nullptr)
		return "unknown";
	std::string result(model);
	free(model);
	return result;
#else
	std::ifstream cpuinfo("/proc/cpuinfo");
	std::string line;
	while (std::getline(cpuinfo, line)) {
		if (line.starts_with("model name")) {
			const std::size_t colon = line.find(':');
			return colon == std::string::npos ? line : line.substr(colon + 2);
		}
	}
	return "unknown";
#endif
}

void printResult(const Options& options, Variant variant, std::size_t workers,
	std::size_t iteration, std::uint64_t schedule_seed, const RunResult& result) {
	const double seconds = static_cast<double>(result.elapsed_ns) / 1.0e9;
	const double requests_per_second = seconds == 0.0 ? 0.0 : result.requests / seconds;
	const double hit_ratio = result.requests == 0 ? 0.0 : static_cast<double>(result.hits) / result.requests;
	const double bytes_per_type = result.canonical_count == 0 ? 0.0 :
		static_cast<double>(result.arena_reserved_bytes) / result.canonical_count;
	std::cout << std::fixed << std::setprecision(3)
		<< "{\"benchmark\":\"canonical_type_interner\",\"variant\":\"" << variantName(variant)
		<< "\",\"workers\":" << workers << ",\"iteration\":" << iteration
		<< ",\"schedule_seed\":" << schedule_seed << ",\"keys\":" << options.key_count
		<< ",\"requests\":" << result.requests << ",\"elapsed_ns\":" << result.elapsed_ns
		<< ",\"requests_per_second\":" << requests_per_second
		<< ",\"hit_ratio\":" << hit_ratio << ",\"cache_hits\":" << result.cache_hits
		<< ",\"misses\":" << result.misses << ",\"duplicate_constructions\":" << result.duplicates
		<< ",\"lock_wait_ns\":" << result.lock_wait_ns << ",\"arena_wait_ns\":" << result.arena_wait_ns
		<< ",\"bytes_lost_to_races\":" << result.bytes_lost
		<< ",\"canonical_types\":" << result.canonical_count
		<< ",\"arena_reserved_bytes\":" << result.arena_reserved_bytes
		<< ",\"bytes_per_canonical_type\":" << bytes_per_type
		<< ",\"peak_rss_bytes\":" << result.peak_rss_bytes
		<< ",\"output_hash\":\"" << std::hex << result.output_hash << std::dec
		<< "\",\"stable\":" << (result.stable ? "true" : "false") << "}\n";
}

std::uint64_t percentile(std::vector<std::uint64_t> values, std::size_t numerator,
	std::size_t denominator) {
	std::sort(values.begin(), values.end());
	const std::size_t index = std::min(values.size() - 1,
		(values.size() * numerator) / denominator);
	return values[index];
}

void printSummary(const Options& options, Variant variant, std::size_t workers,
	const std::vector<RunResult>& results) {
	std::vector<std::uint64_t> elapsed;
	elapsed.reserve(results.size());
	bool stable = true;
	std::uint64_t output_hash = results.front().output_hash;
	for (const RunResult& result : results) {
		elapsed.push_back(result.elapsed_ns);
		stable = stable && result.stable && result.output_hash == output_hash;
	}
	const std::uint64_t median = percentile(elapsed, 1, 2);
	const std::uint64_t q1 = percentile(elapsed, 1, 4);
	const std::uint64_t q3 = percentile(elapsed, 3, 4);
	const std::uint64_t p95 = percentile(elapsed, 95, 100);
	const double requests_per_second = median == 0 ? 0.0 :
		static_cast<double>(options.request_count) * 1.0e9 / static_cast<double>(median);
	std::cout << std::fixed << std::setprecision(3)
		<< "{\"record\":\"summary\",\"benchmark\":\"canonical_type_interner\",\"variant\":\""
		<< variantName(variant) << "\",\"workers\":" << workers
		<< ",\"iterations\":" << results.size() << ",\"median_elapsed_ns\":" << median
		<< ",\"p95_elapsed_ns\":" << p95 << ",\"iqr_elapsed_ns\":" << q3 - q1
		<< ",\"median_requests_per_second\":" << requests_per_second
		<< ",\"output_hash\":\"" << std::hex << output_hash << std::dec
		<< "\",\"stable\":" << (stable ? "true" : "false") << "}\n";
}

} // namespace

int main(int argc, char** argv) {
	try {
		const Options options = parseOptions(argc, argv);
		std::cerr << "canonical_type_interner cpu=\"" << cpuModel()
			<< "\" hw_threads=" << std::thread::hardware_concurrency()
			<< " keys=" << options.key_count << " requests=" << options.request_count
			<< " iterations=" << options.iterations << " warmups=" << options.warmups << "\n";
		const Workload workload = makeWorkload(options.key_count, options.request_count, options.seed);
		const std::array<Variant, 3> variants = {Variant::Mutex, Variant::Sharded, Variant::Cached};
		std::vector<std::uint64_t> baseline_hashes;
		for (Variant variant : variants) {
			for (std::size_t worker_count : options.workers) {
				for (std::size_t warmup = 0; warmup < options.warmups; ++warmup)
					runBenchmark(workload, variant, worker_count, options.seed + warmup, options.publication_delay);
				std::vector<RunResult> results;
				results.reserve(options.iterations);
				for (std::size_t iteration = 0; iteration < options.iterations; ++iteration) {
					const std::uint64_t schedule_seed = options.seed + iteration;
					const RunResult result = runBenchmark(workload, variant, worker_count, schedule_seed,
						options.publication_delay);
					if (variant == Variant::Mutex && worker_count == 1)
						baseline_hashes.push_back(result.output_hash);
					results.push_back(result);
					printResult(options, variant, worker_count, iteration, schedule_seed, result);
				}
				printSummary(options, variant, worker_count, results);
			}
		}
		if (!baseline_hashes.empty()) {
			for (std::uint64_t hash : baseline_hashes)
				if (hash != baseline_hashes.front())
					return 2;
		}
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "interner_benchmark: " << error.what() << "\n";
		return 1;
	}
}
