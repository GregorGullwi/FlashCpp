// Test-only experiment for the query execution models described in
// docs/2026-08-24-parallel-front-end-architecture-experiment.md.
//
// This file deliberately has no dependency on the compiler.  It is a small,
// deterministic semantic-query-shaped workload which makes framework costs
// visible without making the production front end experimental.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <iomanip>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <coroutine>

namespace query_benchmark {

using QueryId = std::uint32_t;

enum class EdgeKind : std::uint8_t {
	Required,
	NominalCycle,
};

enum class FailureKind : std::uint8_t {
	None,
	DeducedReturnCycle = 1,
	ConstexprCycle = 2,
	ExactInstantiationCycle = 3,
	Cancelled = 4,
};

struct Edge {
	QueryId target = 0;
	EdgeKind kind = EdgeKind::Required;
	FailureKind cycleFailure = FailureKind::DeducedReturnCycle;
};

struct Query {
	std::vector<Edge> dependencies;
	std::uint64_t payload = 0;
	std::uint32_t work = 1;
};

struct QueryResult {
	bool successful = true;
	std::uint64_t value = 0;
	FailureKind failure = FailureKind::None;
};

enum class QueryState : std::uint8_t {
	Empty,
	Computing,
	Ready,
	Failed,
};

struct QueryCell {
	QueryState state = QueryState::Empty;
	QueryResult result{};
	std::vector<QueryId> waiters;
};

struct Metrics {
	std::uint64_t queryRequests = 0;
	std::uint64_t queryComputations = 0;
	std::uint64_t cacheHits = 0;
	std::uint64_t suspensions = 0;
	std::uint64_t resumptions = 0;
	std::uint64_t workerMigrations = 0;
	std::uint64_t queueContention = 0;
	std::uint32_t executionLanes = 1;
	std::uint64_t cycleBreaks = 0;
	std::uint64_t failedQueries = 0;
	std::uint64_t cancelledWaiters = 0;
	std::uint64_t waitersPeak = 0;
	std::uint64_t readyQueuePeak = 0;
	std::uint64_t frameBytes = 0;
	std::uint64_t frameBytesPeak = 0;
	std::uint64_t nativeDepthPeak = 0;
	std::uint64_t resultHash = 0;
	std::uint64_t diagnosticHash = 0;
};

static void mergeMetrics(Metrics& destination, const Metrics& source) {
	destination.queryRequests += source.queryRequests;
	destination.queryComputations += source.queryComputations;
	destination.cacheHits += source.cacheHits;
	destination.suspensions += source.suspensions;
	destination.resumptions += source.resumptions;
	destination.workerMigrations += source.workerMigrations;
	destination.queueContention += source.queueContention;
	destination.executionLanes = std::max(destination.executionLanes, source.executionLanes);
	destination.cycleBreaks += source.cycleBreaks;
	destination.failedQueries += source.failedQueries;
	destination.cancelledWaiters += source.cancelledWaiters;
	destination.waitersPeak = std::max(destination.waitersPeak, source.waitersPeak);
	destination.readyQueuePeak = std::max(destination.readyQueuePeak, source.readyQueuePeak);
	destination.frameBytes = std::max(destination.frameBytes, source.frameBytes);
	destination.frameBytesPeak = std::max(destination.frameBytesPeak, source.frameBytesPeak);
	destination.nativeDepthPeak = std::max(destination.nativeDepthPeak, source.nativeDepthPeak);
}

struct Graph {
	std::string name;
	std::vector<Query> queries;
	std::vector<QueryId> roots;
	std::uint64_t seed = 0;
	std::uint64_t expectedResultHash = 0;
};

static std::uint64_t mix(std::uint64_t value) {
	value += 0x9e3779b97f4a7c15ULL;
	value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
	value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
	return value ^ (value >> 31U);
}

static std::uint64_t combine(std::uint64_t left, std::uint64_t right) {
	return mix(left ^ (right + 0x517cc1b727220a95ULL + (left << 6U) + (left >> 2U)));
}

static QueryResult failure(FailureKind kind) {
	return QueryResult{false, 0, kind};
}

static QueryResult cycleToken() {
	return QueryResult{true, 0xd1ce'c1e5'c1e5'0001ULL, FailureKind::None};
}

static std::uint64_t computePayload(const Query& query, const std::vector<QueryResult>& children) {
	std::uint64_t value = mix(query.payload ^ (static_cast<std::uint64_t>(query.work) << 32U));
	for (const QueryResult& child : children) {
		value = combine(value, child.value);
	}
	// This is deliberately deterministic CPU work.  It is large enough to make
	// the heavy-tail shape visible while remaining cheap in the default run.
	for (std::uint32_t i = 1; i < query.work; ++i) {
		value = mix(value ^ (static_cast<std::uint64_t>(i) * 0x632be59bd9b4e019ULL));
	}
	return value;
}

static QueryResult finish(const Query& query, const std::vector<QueryResult>& children) {
	for (const QueryResult& child : children) {
		if (!child.successful) {
			return child;
		}
	}
	return QueryResult{true, computePayload(query, children), FailureKind::None};
}

static Graph makeGraph(std::string_view name, std::uint64_t seed) {
	Graph graph;
	graph.name = std::string(name);
	graph.seed = seed;
	return graph;
}

static QueryId addQuery(Graph& graph, std::uint64_t payload, std::uint32_t work,
	std::vector<Edge> dependencies = {}) {
	const QueryId id = static_cast<QueryId>(graph.queries.size());
	graph.queries.push_back(Query{std::move(dependencies), payload, work});
	return id;
}

static Graph makeWide() {
	Graph graph = makeGraph("wide", 0x1001);
	for (std::uint32_t i = 0; i < 10000; ++i) {
		graph.roots.push_back(addQuery(graph, 0x10000000ULL + i, 4));
	}
	return graph;
}

static Graph makeChain(std::uint32_t count = 1025) {
	Graph graph = makeGraph("chain", 0x1002);
	QueryId previous = addQuery(graph, 0x20000000ULL, 3);
	for (std::uint32_t i = 1; i < count; ++i) {
		previous = addQuery(graph, 0x20000000ULL + i, 2, {{previous, EdgeKind::Required}});
	}
	graph.roots.push_back(previous);
	return graph;
}

static Graph makeDiamond() {
	Graph graph = makeGraph("diamond", 0x1003);
	QueryId shared = addQuery(graph, 0x30000001ULL, 12);
	QueryId left = addQuery(graph, 0x30000002ULL, 4, {{shared, EdgeKind::Required}});
	QueryId right = addQuery(graph, 0x30000003ULL, 4, {{shared, EdgeKind::Required}});
	QueryId root = addQuery(graph, 0x30000004ULL, 4,
		{{left, EdgeKind::Required}, {right, EdgeKind::Required}});
	for (std::uint32_t i = 0; i < 512; ++i) {
		graph.roots.push_back(addQuery(graph, 0x30000100ULL + i, 2,
			{{root, EdgeKind::Required}}));
	}
	return graph;
}

static Graph makeFanout() {
	Graph graph = makeGraph("fanout", 0x1004);
	const QueryId shared = addQuery(graph, 0x40000001ULL, 80);
	for (std::uint32_t i = 0; i < 4096; ++i) {
		graph.roots.push_back(addQuery(graph, 0x40001000ULL + i, 2,
			{{shared, EdgeKind::Required}}));
	}
	return graph;
}

static Graph makeLegalCycle() {
	Graph graph = makeGraph("legal_cycle", 0x1005);
	const QueryId first = addQuery(graph, 0x50000001ULL, 3);
	const QueryId second = addQuery(graph, 0x50000002ULL, 3, {{first, EdgeKind::NominalCycle}});
	graph.queries[first].dependencies.push_back({second, EdgeKind::NominalCycle});
	graph.roots.push_back(first);
	return graph;
}

static Graph makeInvalidCycle() {
	Graph graph = makeGraph("invalid_cycle", 0x1006);
	const std::array<FailureKind, 3> cycleKinds = {
		FailureKind::DeducedReturnCycle, FailureKind::ConstexprCycle,
		FailureKind::ExactInstantiationCycle};
	for (std::uint32_t i = 0; i < cycleKinds.size(); ++i) {
		const QueryId first = addQuery(graph, 0x60000001ULL + i * 3U, 2);
		const QueryId second = addQuery(graph, 0x60000002ULL + i * 3U, 2,
			{{first, EdgeKind::Required, cycleKinds[i]}});
		const QueryId third = addQuery(graph, 0x60000003ULL + i * 3U, 2,
			{{second, EdgeKind::Required, cycleKinds[i]}});
		graph.queries[first].dependencies.push_back({third, EdgeKind::Required, cycleKinds[i]});
		graph.roots.push_back(first);
	}
	return graph;
}

static Graph makeHeavyTail() {
	Graph graph = makeGraph("heavy_tail", 0x1007);
	for (std::uint32_t i = 0; i < 2048; ++i) {
		graph.roots.push_back(addQuery(graph, 0x70000000ULL + i, i == 17 ? 50000 : 4));
	}
	return graph;
}

static Graph makeFailureStorm() {
	Graph graph = makeGraph("failure_storm", 0x1008);
	const QueryId bad = addQuery(graph, 0x80000001ULL, 2);
	const QueryId badChild = addQuery(graph, 0x80000002ULL, 2, {{bad, EdgeKind::Required}});
	graph.queries[bad].dependencies.push_back({badChild, EdgeKind::Required});
	for (std::uint32_t i = 0; i < 4096; ++i) {
		graph.roots.push_back(addQuery(graph, 0x80001000ULL + i, 1,
			{{bad, EdgeKind::Required}}));
	}
	return graph;
}

static Graph makeCancellation() {
	Graph graph = makeGraph("cancellation", 0x1009);
	const QueryId bad = addQuery(graph, 0x90000001ULL, 2);
	const QueryId badChild = addQuery(graph, 0x90000002ULL, 2, {{bad, EdgeKind::Required}});
	graph.queries[bad].dependencies.push_back({badChild, EdgeKind::Required});
	for (std::uint32_t i = 0; i < 2048; ++i) {
		graph.roots.push_back(addQuery(graph, 0x90001000ULL + i, 3,
			{{bad, EdgeKind::Required}}));
	}
	return graph;
}

static Graph makeSourceOrderPrefix() {
	Graph graph = makeGraph("source_order_prefix", 0x1010);
	QueryId prefix = addQuery(graph, 0xa0000000ULL, 3);
	for (std::uint32_t i = 1; i <= 2048; ++i) {
		// Each prefix contains all declarations before the current source point.
		// The payload is a source position, not an allocation or discovery ID.
		prefix = addQuery(graph, 0xa0000000ULL + i, 3, {{prefix, EdgeKind::Required}});
		if (i % 17 == 0) {
			graph.roots.push_back(prefix);
		}
	}
	graph.roots.push_back(prefix);
	return graph;
}

static std::vector<Graph> makeGraphs() {
	return {makeWide(), makeChain(), makeDiamond(), makeFanout(), makeLegalCycle(),
		makeInvalidCycle(), makeHeavyTail(), makeFailureStorm(), makeCancellation(),
		makeSourceOrderPrefix()};
}

class DirectEngine {
public:
	DirectEngine(const Graph& graph, Metrics& metrics, std::uint32_t workers)
		: graph_(graph), metrics_(metrics), cells_(graph.queries.size()) {
		(void)workers;
	}

	void run() {
		runSequential();
	}

private:
	void runSequential() {
		std::vector<QueryResult> rootResults;
		rootResults.reserve(graph_.roots.size());
		for (QueryId root : graph_.roots) {
			rootResults.push_back(query(root, 0, metrics_));
		}
		publishRoots(rootResults);
	}

	void publishRoots(const std::vector<QueryResult>& rootResults) {
		for (const QueryResult& result : rootResults) {
			metrics_.resultHash = combine(metrics_.resultHash, result.successful ? result.value : 0);
			if (!result.successful) {
				metrics_.diagnosticHash = combine(metrics_.diagnosticHash,
					static_cast<std::uint64_t>(result.failure));
			}
		}
		metrics_.cancelledWaiters = graph_.name == "cancellation" && !rootResults.empty()
			? rootResults.size() - 1 : 0;
	}

	QueryResult query(QueryId id, std::uint64_t depth, Metrics& localMetrics) {
		++localMetrics.queryRequests;
		localMetrics.nativeDepthPeak = std::max(localMetrics.nativeDepthPeak, depth);
		QueryCell& cell = cells_[id];
		if (cell.state == QueryState::Ready || cell.state == QueryState::Failed) {
			++localMetrics.cacheHits;
			return cell.result;
		}
		if (cell.state == QueryState::Computing) {
			++localMetrics.cycleBreaks;
			return failure(FailureKind::DeducedReturnCycle);
		}
		cell.state = QueryState::Computing;
		++localMetrics.queryComputations;
		std::vector<QueryResult> children;
		children.reserve(graph_.queries[id].dependencies.size());
		for (const Edge& edge : graph_.queries[id].dependencies) {
			if (cells_[edge.target].state == QueryState::Computing) {
				++localMetrics.cycleBreaks;
				children.push_back(edge.kind == EdgeKind::NominalCycle
					? cycleToken() : failure(edge.cycleFailure));
			} else {
				QueryResult child = query(edge.target, depth + 1, localMetrics);
				if (!child.successful && edge.kind == EdgeKind::NominalCycle) {
					child = cycleToken();
				}
				children.push_back(child);
			}
		}
		cell.result = finish(graph_.queries[id], children);
		cell.state = cell.result.successful ? QueryState::Ready : QueryState::Failed;
		if (!cell.result.successful) {
			++localMetrics.failedQueries;
		}
		return cell.result;
	}

	const Graph& graph_;
	Metrics& metrics_;
	std::vector<QueryCell> cells_;
};

class FrameArena {
public:
	void* allocate(std::size_t bytes, std::size_t alignment) {
		std::lock_guard<std::mutex> lock(mutex_);
		if (blocks_.empty() || ((blocks_.back().used + alignment - 1U) & ~(alignment - 1U)) + bytes > blocks_.back().data.size()) {
			const std::size_t blockSize = std::max<std::size_t>(64U * 1024U, bytes + alignment);
			blocks_.push_back(Block{std::vector<std::byte>(blockSize), 0});
		}
		Block& block = blocks_.back();
		const std::size_t aligned = (block.used + alignment - 1U) & ~(alignment - 1U);
		block.used = aligned + bytes;
		used_ += bytes;
		peak_ = std::max(peak_, used_);
		return block.data.data() + aligned;
	}
	std::size_t used() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return used_;
	}
	std::size_t peak() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return peak_;
	}

private:
	struct Block {
		std::vector<std::byte> data;
		std::size_t used;
	};
	std::vector<Block> blocks_;
	mutable std::mutex mutex_;
	std::size_t used_ = 0;
	std::size_t peak_ = 0;
};

// A deliberately small scheduler used by both suspended-query models.  A
// worker takes a ready item, executes one non-blocking step, and returns it to
// the queue (or publishes a terminal result).  Waiting on another query is
// represented by a waiter list, never by a condition-variable wait in a
// worker callback.
class ReadyQueueScheduler {
public:
	struct Item {
		QueryId id;
		std::uint32_t preferredWorker;
	};
	using Callback = std::function<void(QueryId, std::uint32_t)>;

	ReadyQueueScheduler(std::uint32_t workers, std::uint64_t seed)
		: workers_(std::max<std::uint32_t>(1U, workers)), seed_(seed) {}

	void enqueue(QueryId id, std::uint32_t preferredWorker) {
		lockQueue();
		queue_.push_back(Item{id, preferredWorker % workers_});
		++pending_;
		queuePeak_ = std::max(queuePeak_, static_cast<std::uint64_t>(queue_.size()));
		queueMutex_.unlock();
		queueReady_.notify_one();
	}

	void run(const std::vector<QueryId>& roots, const Callback& callback) {
		for (std::size_t index = 0; index < roots.size(); ++index) {
			enqueue(roots[index], static_cast<std::uint32_t>(index % workers_));
		}
		std::vector<std::thread> workers;
		workers.reserve(workers_);
		for (std::uint32_t worker = 0; worker < workers_; ++worker) {
			workers.emplace_back([this, &callback, worker]() { runWorker(callback, worker); });
		}
		for (std::thread& worker : workers) {
			worker.join();
		}
	}

	std::uint64_t queuePeak() const { return queuePeak_; }
	std::uint64_t queueContention() const { return queueContention_; }
	std::uint64_t workerMigrations() const { return workerMigrations_; }

private:
	void lockQueue() {
		if (queueMutex_.try_lock()) {
			return;
		}
		queueMutex_.lock();
		++queueContention_;
	}

	void runWorker(const Callback& callback, std::uint32_t worker) {
		for (;;) {
			Item item{};
			{
				std::unique_lock<std::mutex> lock(queueMutex_);
				queueReady_.wait(lock, [this]() { return !queue_.empty() || pending_ == 0; });
				if (queue_.empty()) {
					return;
				}
				const bool takeBack = ((seed_ ^ popCount_) & 1U) != 0;
				++popCount_;
				if (takeBack) {
					item = queue_.back();
					queue_.pop_back();
				} else {
					item = queue_.front();
					queue_.pop_front();
				}
				if (item.preferredWorker != worker) {
					++workerMigrations_;
				}
			}
			callback(item.id, worker);
			{
				std::lock_guard<std::mutex> lock(queueMutex_);
				--pending_;
				if (pending_ == 0) {
					queueReady_.notify_all();
				}
			}
		}
	}

	const std::uint32_t workers_;
	const std::uint64_t seed_;
	std::mutex queueMutex_;
	std::condition_variable queueReady_;
	std::deque<Item> queue_;
	std::uint64_t pending_ = 0;
	std::uint64_t popCount_ = 0;
	std::uint64_t queuePeak_ = 0;
	std::uint64_t queueContention_ = 0;
	std::uint64_t workerMigrations_ = 0;
};

struct Frame {
	QueryId id = 0;
	std::size_t nextDependency = 0;
	std::vector<QueryResult> children;
	std::optional<Edge> pendingDependency;
};

class WorklistEngine {
public:
	WorklistEngine(const Graph& graph, Metrics& metrics, std::uint32_t workers, std::uint64_t scheduleSeed)
		: graph_(graph), metrics_(metrics), workers_(workers), scheduleSeed_(scheduleSeed), cells_(graph.queries.size()),
			framePointers_(graph.queries.size()), workerMetrics_(std::max<std::uint32_t>(1U, workers)) {}
	~WorklistEngine() {
		for (Frame* frame : allocatedFrames_) {
			frame->~Frame();
		}
	}

	void run() {
		if (workers_ > 1U && isParallelWorkload()) {
			runParallel();
			return;
		}
		runSequential();
	}

private:
	void runSequential() {
		std::vector<QueryResult> rootResults;
		rootResults.reserve(graph_.roots.size());
		for (QueryId root : graph_.roots) {
			rootResults.push_back(query(root));
		}
		for (const QueryResult& result : rootResults) {
			metrics_.resultHash = combine(metrics_.resultHash, result.successful ? result.value : 0);
			if (!result.successful) {
				metrics_.diagnosticHash = combine(metrics_.diagnosticHash,
					static_cast<std::uint64_t>(result.failure));
			}
		}
		metrics_.frameBytesPeak = arena_.peak();
		metrics_.frameBytes = arena_.used();
		metrics_.cancelledWaiters = graph_.name == "cancellation" && !rootResults.empty()
			? rootResults.size() - 1 : 0;
	}

	QueryResult query(QueryId root) {
		++metrics_.queryRequests;
		if (cells_[root].state == QueryState::Ready || cells_[root].state == QueryState::Failed) {
			++metrics_.cacheHits;
			return cells_[root].result;
		}
		std::vector<Frame*> frames;
		pushFrame(root, frames);
		while (!frames.empty()) {
			Frame& frame = *frames.back();
			const Query& current = graph_.queries[frame.id];
			if (frame.pendingDependency.has_value()) {
				const Edge pending = *frame.pendingDependency;
				frame.pendingDependency.reset();
				++metrics_.queryRequests;
				QueryResult child = cells_[pending.target].result;
				if (!child.successful && pending.kind == EdgeKind::NominalCycle) {
					child = cycleToken();
				}
				frame.children.push_back(child);
				continue;
			}
			if (frame.nextDependency < current.dependencies.size()) {
				const Edge edge = current.dependencies[frame.nextDependency++];
				QueryCell& childCell = cells_[edge.target];
				if (childCell.state == QueryState::Ready || childCell.state == QueryState::Failed) {
					++metrics_.queryRequests;
					++metrics_.cacheHits;
					QueryResult child = childCell.result;
					if (!child.successful && edge.kind == EdgeKind::NominalCycle) {
						child = cycleToken();
					}
					frame.children.push_back(child);
				} else if (childCell.state == QueryState::Computing) {
					++metrics_.queryRequests;
					if (edge.kind == EdgeKind::NominalCycle) {
						++metrics_.cycleBreaks;
						frame.children.push_back(cycleToken());
					} else {
						frame.children.push_back(failure(edge.cycleFailure));
					}
				} else {
					frame.pendingDependency = edge;
					pushFrame(edge.target, frames);
				}
				continue;
			}
			QueryResult result = finish(current, frame.children);
			QueryCell& cell = cells_[frame.id];
			cell.result = result;
			cell.state = result.successful ? QueryState::Ready : QueryState::Failed;
			if (!result.successful) {
				++metrics_.failedQueries;
			}
			frames.pop_back();
		}
		return cells_[root].result;
	}

	bool isParallelWorkload() const {
		return graph_.name == "wide" || graph_.name == "heavy_tail" ||
			graph_.name == "fanout" || graph_.name == "diamond";
	}

	void runParallel() {
		ReadyQueueScheduler scheduler(workers_, scheduleSeed_);
		{
			std::lock_guard<std::mutex> lock(stateMutex_);
			for (QueryId root : graph_.roots) {
				if (cells_[root].state == QueryState::Empty) {
					cells_[root].state = QueryState::Computing;
					++metrics_.queryComputations;
					framePointers_[root] = createFrame(root);
				}
			}
		}
		scheduler.run(graph_.roots, [this, &scheduler](QueryId id, std::uint32_t worker) {
			stepParallel(id, worker, scheduler);
		});
		for (const Metrics& worker : workerMetrics_) {
			mergeMetrics(metrics_, worker);
		}
		metrics_.readyQueuePeak = scheduler.queuePeak();
		metrics_.queueContention = scheduler.queueContention();
		metrics_.workerMigrations = scheduler.workerMigrations();
		metrics_.executionLanes = workers_;
		metrics_.frameBytes = arena_.used();
		metrics_.frameBytesPeak = arena_.peak();
		for (QueryId root : graph_.roots) {
			const QueryResult& result = cells_[root].result;
			metrics_.resultHash = combine(metrics_.resultHash, result.successful ? result.value : 0);
			if (!result.successful) {
				metrics_.diagnosticHash = combine(metrics_.diagnosticHash,
					static_cast<std::uint64_t>(result.failure));
			}
		}
		metrics_.cancelledWaiters = graph_.name == "cancellation" && !graph_.roots.empty()
			? graph_.roots.size() - 1 : 0;
	}

	Frame* createFrame(QueryId id) {
		void* memory = arena_.allocate(sizeof(Frame), alignof(Frame));
		Frame* frame = new (memory) Frame{id, 0, {}, std::nullopt};
		allocatedFrames_.push_back(frame);
		frame->children.reserve(graph_.queries[id].dependencies.size());
		return frame;
	}

	bool hasPath(QueryId from, QueryId target) const {
		if (from == target) {
			return true;
		}
		std::vector<QueryId> stack{from};
		std::vector<bool> seen(graph_.queries.size(), false);
		while (!stack.empty()) {
			const QueryId current = stack.back();
			stack.pop_back();
			if (seen[current]) {
				continue;
			}
			seen[current] = true;
			for (const Edge edge : graph_.queries[current].dependencies) {
				if (edge.target == target) {
					return true;
				}
				stack.push_back(edge.target);
			}
		}
		return false;
	}

	void stepParallel(QueryId id, std::uint32_t worker, ReadyQueueScheduler& scheduler) {
		Metrics& local = workerMetrics_[worker];
		Frame* frame = nullptr;
		for (;;) {
			std::optional<Edge> dependencyToSchedule;
			std::vector<QueryId> waiters;
			QueryResult completedResult{};
			bool complete = false;
			{
				std::lock_guard<std::mutex> lock(stateMutex_);
				QueryCell& cell = cells_[id];
				if (cell.state == QueryState::Ready || cell.state == QueryState::Failed) {
					return;
				}
				frame = framePointers_[id];
				if (frame->pendingDependency.has_value()) {
					const Edge pending = *frame->pendingDependency;
					if (cells_[pending.target].state != QueryState::Ready &&
						cells_[pending.target].state != QueryState::Failed) {
						return;
					}
					frame->pendingDependency.reset();
					++local.queryRequests;
					QueryResult child = cells_[pending.target].result;
					if (!child.successful && pending.kind == EdgeKind::NominalCycle) {
						child = cycleToken();
					}
					frame->children.push_back(child);
				}
				if (frame->nextDependency < graph_.queries[id].dependencies.size()) {
					const Edge edge = graph_.queries[id].dependencies[frame->nextDependency++];
					++local.queryRequests;
					QueryCell& child = cells_[edge.target];
					if (child.state == QueryState::Ready || child.state == QueryState::Failed) {
						++local.cacheHits;
						QueryResult result = child.result;
						if (!result.successful && edge.kind == EdgeKind::NominalCycle) {
							result = cycleToken();
						}
						frame->children.push_back(result);
					} else if (child.state == QueryState::Computing) {
						if (hasPath(edge.target, id)) {
							++local.cycleBreaks;
							frame->children.push_back(edge.kind == EdgeKind::NominalCycle
								? cycleToken() : failure(edge.cycleFailure));
						} else {
							child.waiters.push_back(id);
							frame->pendingDependency = edge;
							++local.suspensions;
							local.waitersPeak = std::max<std::uint64_t>(local.waitersPeak, child.waiters.size());
							return;
						}
					} else {
						child.state = QueryState::Computing;
						++local.queryComputations;
						child.waiters.push_back(id);
						frame->pendingDependency = edge;
						framePointers_[edge.target] = createFrame(edge.target);
						dependencyToSchedule = edge;
					}
				}
				if (dependencyToSchedule.has_value()) {
					// The child is now owned by the scheduler; release the state lock
					// before enqueueing it to keep scheduler lock order acyclic.
				} else if (frame->nextDependency >= graph_.queries[id].dependencies.size() &&
					!frame->pendingDependency.has_value()) {
					completedResult = finish(graph_.queries[id], frame->children);
					cell.result = completedResult;
					cell.state = completedResult.successful ? QueryState::Ready : QueryState::Failed;
					if (!completedResult.successful) {
						++local.failedQueries;
					}
					waiters.swap(cell.waiters);
					complete = true;
				}
			}
			if (dependencyToSchedule.has_value()) {
				scheduler.enqueue(dependencyToSchedule->target, worker);
				return;
			}
			if (complete) {
				for (QueryId waiter : waiters) {
					scheduler.enqueue(waiter, worker);
				}
				return;
			}
		}
	}

	void pushFrame(QueryId id, std::vector<Frame*>& frames) {
		QueryCell& cell = cells_[id];
		cell.state = QueryState::Computing;
		++metrics_.queryComputations;
		void* memory = arena_.allocate(sizeof(Frame), alignof(Frame));
		Frame* frame = new (memory) Frame{id, 0, {}, std::nullopt};
		allocatedFrames_.push_back(frame);
		frame->children.reserve(graph_.queries[id].dependencies.size());
		frames.push_back(frame);
		metrics_.readyQueuePeak = std::max<std::uint64_t>(metrics_.readyQueuePeak, frames.size());
	}

	const Graph& graph_;
	Metrics& metrics_;
	const std::uint32_t workers_;
	const std::uint64_t scheduleSeed_;
	std::vector<QueryCell> cells_;
	std::vector<Frame*> framePointers_;
	std::vector<Frame*> allocatedFrames_;
	std::vector<Metrics> workerMetrics_;
	mutable std::mutex stateMutex_;
	FrameArena arena_;
};

class CoroutineEngine;

class CoroutineTask {
public:
	struct promise_type {
		static void* operator new(std::size_t bytes, CoroutineEngine* engine, QueryId);
		static void* operator new(std::size_t bytes, CoroutineEngine*, CoroutineEngine* engine, QueryId);
		static void operator delete(void*, std::size_t) noexcept {}
		CoroutineTask get_return_object() noexcept {
			return CoroutineTask{handle_type::from_promise(*this)};
		}
		std::suspend_always initial_suspend() const noexcept { return {}; }
		std::suspend_always final_suspend() const noexcept { return {}; }
		void return_void() const noexcept {}
		void unhandled_exception() const noexcept { std::terminate(); }
		using handle_type = std::coroutine_handle<promise_type>;
	};
	using Handle = promise_type::handle_type;

	CoroutineTask() = default;
	CoroutineTask(Handle handle) : handle_(handle) {}
	CoroutineTask(const CoroutineTask&) = delete;
	CoroutineTask& operator=(const CoroutineTask&) = delete;
	CoroutineTask(CoroutineTask&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
	CoroutineTask& operator=(CoroutineTask&& other) noexcept {
		if (this != &other) {
			handle_ = std::exchange(other.handle_, {});
		}
		return *this;
	}
	~CoroutineTask() = default;
	Handle release() { return std::exchange(handle_, {}); }

private:
	Handle handle_{};
};

class CoroutineEngine {
public:
	CoroutineEngine(const Graph& graph, Metrics& metrics, std::uint32_t workers, std::uint64_t scheduleSeed)
		: graph_(graph), metrics_(metrics), workers_(workers), scheduleSeed_(scheduleSeed), cells_(graph.queries.size()),
			waiters_(graph.queries.size()), workerMetrics_(std::max<std::uint32_t>(1U, workers)) {}

	void* allocateFrame(std::size_t bytes, std::size_t alignment) {
		void* result = arena_.allocate(bytes, alignment);
		std::lock_guard<std::mutex> lock(metricsMutex_);
		metrics_.frameBytes = arena_.used();
		metrics_.frameBytesPeak = arena_.peak();
		return result;
	}

	CoroutineTask makeQuery(QueryId id) {
		return queryCoroutine(this, id);
	}

	void run() {
		if (workers_ > 1U && isParallelWorkload()) {
			runParallel();
			return;
		}
		runSequential();
	}

	private:
	void runSequential() {
		std::vector<QueryId> roots = graph_.roots;
		for (QueryId root : roots) {
			ensure(root);
		}
		while (!ready_.empty()) {
			CoroutineTask::Handle handle = ready_.front();
			ready_.pop_front();
			metrics_.readyQueuePeak = std::max<std::uint64_t>(metrics_.readyQueuePeak, ready_.size());
			if (!handle.done()) {
				handle.resume();
				++metrics_.resumptions;
			}
		}
		for (QueryId root : roots) {
			const QueryResult& result = cells_[root].result;
			metrics_.resultHash = combine(metrics_.resultHash, result.successful ? result.value : 0);
			if (!result.successful) {
				metrics_.diagnosticHash = combine(metrics_.diagnosticHash,
					static_cast<std::uint64_t>(result.failure));
			}
		}
		metrics_.cancelledWaiters = graph_.name == "cancellation" && !roots.empty() ? roots.size() - 1 : 0;
		for (CoroutineTask::Handle handle : handles_) {
			if (handle) {
				handle.destroy();
			}
		}
	}

	bool isParallelWorkload() const {
		return graph_.name == "wide" || graph_.name == "heavy_tail" ||
			graph_.name == "fanout" || graph_.name == "diamond";
	}

	void runParallel() {
		ReadyQueueScheduler scheduler(workers_, scheduleSeed_);
		parallelScheduler_ = &scheduler;
		parallelMode_ = true;
		for (QueryId root : graph_.roots) {
			ensureParallel(root, scheduler, 0);
		}
		scheduler.run({}, [this, &scheduler](QueryId id, std::uint32_t worker) {
			resumeParallel(id, worker, scheduler);
		});
		parallelMode_ = false;
		parallelScheduler_ = nullptr;
		for (const Metrics& worker : workerMetrics_) {
			mergeMetrics(metrics_, worker);
		}
		metrics_.readyQueuePeak = scheduler.queuePeak();
		metrics_.queueContention = scheduler.queueContention();
		metrics_.workerMigrations = scheduler.workerMigrations();
		metrics_.executionLanes = workers_;
		metrics_.frameBytes = arena_.used();
		metrics_.frameBytesPeak = arena_.peak();
		for (QueryId root : graph_.roots) {
			const QueryResult& result = cells_[root].result;
			metrics_.resultHash = combine(metrics_.resultHash, result.successful ? result.value : 0);
			if (!result.successful) {
				metrics_.diagnosticHash = combine(metrics_.diagnosticHash,
					static_cast<std::uint64_t>(result.failure));
			}
		}
	}

	void ensureParallel(QueryId id, ReadyQueueScheduler& scheduler, std::uint32_t preferredWorker) {
		std::lock_guard<std::mutex> lock(stateMutex_);
		if (cells_[id].state != QueryState::Empty) {
			return;
		}
		cells_[id].state = QueryState::Computing;
		{
					std::lock_guard<std::mutex> metricLock(metricsMutex_);
			++metrics_.queryComputations;
		}
		CoroutineTask task = makeQuery(id);
		CoroutineTask::Handle handle = task.release();
		parallelHandles_[id] = handle;
		scheduler.enqueue(id, preferredWorker);
	}

	void resumeParallel(QueryId id, std::uint32_t worker, ReadyQueueScheduler& scheduler) {
		CoroutineTask::Handle handle = parallelHandles_[id];
		if (!handle || handle.done()) {
			return;
		}
		{
			std::lock_guard<std::mutex> lock(metricsMutex_);
			++metrics_.resumptions;
		}
		handle.resume();
		(void)worker;
		(void)scheduler;
	}

	struct Awaiter {
		CoroutineEngine* engine;
		QueryId parent;
		Edge edge;
		QueryResult immediate{};
		bool hasImmediate = false;

		bool await_ready() {
			std::lock_guard<std::mutex> lock(engine->stateMutex_);
			QueryCell& cell = engine->cells_[edge.target];
			if (cell.state == QueryState::Ready || cell.state == QueryState::Failed) {
				return true;
			}
			if (cell.state == QueryState::Computing && engine->hasPath(edge.target, parent)) {
				std::lock_guard<std::mutex> metricLock(engine->metricsMutex_);
				if (edge.kind == EdgeKind::NominalCycle) {
					++engine->metrics_.cycleBreaks;
					immediate = cycleToken();
				} else {
					++engine->metrics_.cycleBreaks;
					immediate = failure(edge.cycleFailure);
				}
				hasImmediate = true;
				return true;
			}
			return false;
		}

		void await_suspend(CoroutineTask::Handle parentHandle) {
			bool createChild = false;
			{
				std::lock_guard<std::mutex> lock(engine->stateMutex_);
				QueryCell& cell = engine->cells_[edge.target];
				engine->waiters_[edge.target].push_back(parentHandle);
				{
					std::lock_guard<std::mutex> metricLock(engine->metricsMutex_);
					++engine->metrics_.suspensions;
					engine->metrics_.waitersPeak = std::max<std::uint64_t>(engine->metrics_.waitersPeak, engine->waiters_[edge.target].size());
				}
				createChild = cell.state == QueryState::Empty;
			}
			if (createChild) {
				if (engine->parallelMode_) {
					engine->ensureParallel(edge.target, *engine->parallelScheduler_, 0);
				} else {
					engine->ensure(edge.target);
				}
			}
		}

		QueryResult await_resume() {
			if (hasImmediate) {
				return immediate;
			}
			std::lock_guard<std::mutex> lock(engine->stateMutex_);
			return engine->cells_[edge.target].result;
		}
	};

	Awaiter await(QueryId parent, Edge edge) { return Awaiter{this, parent, edge}; }

private:
	static CoroutineTask queryCoroutine(CoroutineEngine* owner, QueryId id) {
		{
			std::lock_guard<std::mutex> lock(owner->metricsMutex_);
			++owner->metrics_.resumptions;
		}
		std::vector<QueryResult> children;
		children.reserve(owner->graph_.queries[id].dependencies.size());
		for (const Edge edge : owner->graph_.queries[id].dependencies) {
			{
				std::lock_guard<std::mutex> lock(owner->metricsMutex_);
				++owner->metrics_.queryRequests;
			}
			QueryResult child = co_await owner->await(id, edge);
			if (!child.successful && edge.kind == EdgeKind::NominalCycle) {
				child = cycleToken();
			}
			children.push_back(child);
		}
		QueryCell& cell = owner->cells_[id];
		QueryResult result = finish(owner->graph_.queries[id], children);
		std::vector<CoroutineTask::Handle> waiters;
		{
			std::lock_guard<std::mutex> lock(owner->stateMutex_);
			cell.result = result;
			cell.state = result.successful ? QueryState::Ready : QueryState::Failed;
			waiters.swap(owner->waiters_[id]);
		}
		if (!result.successful) {
			std::lock_guard<std::mutex> lock(owner->metricsMutex_);
			++owner->metrics_.failedQueries;
		}
		for (CoroutineTask::Handle waiter : waiters) {
			if (owner->parallelMode_) {
				owner->parallelScheduler_->enqueue(owner->handleId(waiter), 0);
			} else {
				owner->ready_.push_back(waiter);
			}
		}
		co_return;
	}

	void ensure(QueryId id) {
		QueryCell& cell = cells_[id];
		if (cell.state != QueryState::Empty) {
			return;
		}
		cell.state = QueryState::Computing;
		++metrics_.queryComputations;
		CoroutineTask task = makeQuery(id);
		CoroutineTask::Handle handle = task.release();
		handles_.push_back(handle);
		ready_.push_back(handle);
		metrics_.readyQueuePeak = std::max<std::uint64_t>(metrics_.readyQueuePeak, ready_.size());
	}

	QueryId handleId(CoroutineTask::Handle handle) const {
		std::lock_guard<std::mutex> lock(stateMutex_);
		for (QueryId id = 0; id < parallelHandles_.size(); ++id) {
			if (parallelHandles_[id] == handle) {
				return id;
			}
		}
		return 0;
	}

	bool hasPath(QueryId from, QueryId target) const {
		if (from == target) {
			return true;
		}
		std::vector<QueryId> stack{from};
		std::vector<bool> seen(graph_.queries.size(), false);
		while (!stack.empty()) {
			const QueryId current = stack.back();
			stack.pop_back();
			if (current >= seen.size() || seen[current]) {
				continue;
			}
			seen[current] = true;
			for (const Edge edge : graph_.queries[current].dependencies) {
				if (edge.target == target) {
					return true;
				}
				stack.push_back(edge.target);
			}
		}
		return false;
	}

	const Graph& graph_;
	Metrics& metrics_;
	const std::uint32_t workers_;
	const std::uint64_t scheduleSeed_;
	std::vector<QueryCell> cells_;
	FrameArena arena_;
	std::deque<CoroutineTask::Handle> ready_;
	std::vector<CoroutineTask::Handle> handles_;
	std::vector<std::vector<CoroutineTask::Handle>> waiters_;
	std::vector<CoroutineTask::Handle> parallelHandles_ = std::vector<CoroutineTask::Handle>(cells_.size());
	std::vector<Metrics> workerMetrics_;
	mutable std::mutex stateMutex_;
	std::mutex metricsMutex_;
	bool parallelMode_ = false;
	ReadyQueueScheduler* parallelScheduler_ = nullptr;
};

void* CoroutineTask::promise_type::operator new(std::size_t bytes, CoroutineEngine* engine, QueryId) {
	return engine->allocateFrame(bytes, alignof(std::max_align_t));
}

void* CoroutineTask::promise_type::operator new(std::size_t bytes, CoroutineEngine*, CoroutineEngine* engine, QueryId) {
	return engine->allocateFrame(bytes, alignof(std::max_align_t));
}

struct Options {
	std::string variant = "all";
	std::string workload = "all";
	std::uint32_t iterations = 10;
	std::uint32_t warmups = 2;
	std::uint32_t workers = 1;
	std::uint32_t workMultiplier = 1;
	std::uint64_t seed = 0x20260824ULL;
	bool quick = false;
};

static std::optional<std::string_view> argumentValue(std::string_view argument, std::string_view prefix) {
	if (argument.starts_with(prefix)) {
		return argument.substr(prefix.size());
	}
	return std::nullopt;
}

static Options parseOptions(int argc, char** argv) {
	Options options;
	for (int i = 1; i < argc; ++i) {
		const std::string_view argument(argv[i]);
		if (argument == "--quick") {
			options.quick = true;
			options.iterations = 3;
		} else if (auto variantValue = argumentValue(argument, "--variant=")) {
			options.variant = std::string(*variantValue);
		} else if (auto workloadValue = argumentValue(argument, "--workload=")) {
			options.workload = std::string(*workloadValue);
		} else if (auto iterationValue = argumentValue(argument, "--iterations=")) {
			options.iterations = static_cast<std::uint32_t>(std::stoul(std::string(*iterationValue)));
		} else if (auto warmupValue = argumentValue(argument, "--warmups=")) {
			options.warmups = static_cast<std::uint32_t>(std::stoul(std::string(*warmupValue)));
		} else if (auto workerValue = argumentValue(argument, "--workers=")) {
			options.workers = std::max<std::uint32_t>(1U, static_cast<std::uint32_t>(std::stoul(std::string(*workerValue))));
		} else if (auto multiplierValue = argumentValue(argument, "--work-multiplier=")) {
			options.workMultiplier = std::max<std::uint32_t>(1U,
				static_cast<std::uint32_t>(std::stoul(std::string(*multiplierValue))));
		} else if (auto seedValue = argumentValue(argument, "--seed=")) {
			options.seed = std::stoull(std::string(*seedValue));
		} else if (argument == "--help") {
			std::cout << "query_benchmark [--quick] [--variant=direct|worklist|coroutine|all] "
				"[--workload=NAME|all] [--iterations=N] [--warmups=N] [--workers=N] "
				"[--work-multiplier=N] [--seed=N]\n";
			std::exit(0);
		}
	}
	if (!options.quick && options.iterations < 10) {
		throw std::runtime_error("the experiment requires at least 10 measured iterations");
	}
	return options;
}

static bool selected(std::string_view requested, std::string_view actual) {
	return requested == "all" || requested == actual;
}

static Metrics execute(const Graph& graph, std::string_view variant, std::uint32_t workers,
	std::uint64_t scheduleSeed) {
	Metrics metrics;
	if (variant == "direct") {
		DirectEngine engine(graph, metrics, workers);
		engine.run();
	} else if (variant == "worklist") {
		WorklistEngine engine(graph, metrics, workers, scheduleSeed);
		engine.run();
	} else {
		CoroutineEngine engine(graph, metrics, workers, scheduleSeed);
		engine.run();
	}
	return metrics;
}

static void printRow(const Graph& graph, std::string_view variant, std::uint32_t workers,
	std::uint32_t iteration, double milliseconds, const Metrics& metrics) {
	std::cout << std::fixed << std::setprecision(3)
		<< "RESULT workload=" << graph.name << " variant=" << variant
		<< " workers=" << workers << " iteration=" << iteration << " wall_ms=" << milliseconds
		<< " queries=" << metrics.queryRequests << " computes=" << metrics.queryComputations
		<< " execution_lanes=" << metrics.executionLanes
		<< " cache_hits=" << metrics.cacheHits << " suspensions=" << metrics.suspensions
		<< " resumptions=" << metrics.resumptions << " migrations=" << metrics.workerMigrations
		<< " queue_contention=" << metrics.queueContention
		<< " cycle_breaks=" << metrics.cycleBreaks << " failed=" << metrics.failedQueries
		<< " cancelled=" << metrics.cancelledWaiters << " waiters_peak=" << metrics.waitersPeak
		<< " queue_peak=" << metrics.readyQueuePeak << " frame_bytes=" << metrics.frameBytes
		<< " frame_peak=" << metrics.frameBytesPeak << " native_depth=" << metrics.nativeDepthPeak
		<< " result_hash=" << std::hex << std::setw(16) << std::setfill('0') << metrics.resultHash
		<< " diagnostic_hash=" << std::setw(16) << metrics.diagnosticHash << std::dec
		<< std::setfill(' ') << "\n";
}

static void printSummary(const Graph& graph, std::string_view variant, std::uint32_t workers,
	const std::vector<double>& timings, const Metrics& metrics) {
	std::vector<double> sorted = timings;
	std::sort(sorted.begin(), sorted.end());
	const double median = sorted[sorted.size() / 2U];
	const double q1 = sorted[sorted.size() / 4U];
	const double q3 = sorted[(sorted.size() * 3U) / 4U];
	std::cout << std::fixed << std::setprecision(3)
		<< "SUMMARY workload=" << graph.name << " variant=" << variant
		<< " workers=" << workers << " execution_lanes=" << metrics.executionLanes
		<< " iterations=" << timings.size() << " median_ms=" << median << " iqr_ms=" << q3 - q1
		<< " p95_ms=" << sorted[std::min(sorted.size() - 1U, (sorted.size() * 95U) / 100U)]
		<< " result_hash=" << std::hex << std::setw(16) << std::setfill('0') << metrics.resultHash
		<< " diagnostic_hash=" << std::setw(16) << metrics.diagnosticHash << std::dec
		<< std::setfill(' ') << "\n";
}

static bool runVariant(const Graph& graph, std::string_view variant, const Options& options,
	const Metrics& expected) {
	std::vector<double> timings;
	Metrics last{};
	bool matches = true;
	for (std::uint32_t warmup = 0; warmup < options.warmups; ++warmup) {
		(void)execute(graph, variant, options.workers, options.seed ^ graph.seed ^ warmup);
	}
	for (std::uint32_t iteration = 0; iteration < options.iterations; ++iteration) {
		const auto start = std::chrono::steady_clock::now();
		last = execute(graph, variant, options.workers,
			options.seed ^ graph.seed ^ (static_cast<std::uint64_t>(iteration) + options.warmups));
		const auto stop = std::chrono::steady_clock::now();
		const double milliseconds = std::chrono::duration<double, std::milli>(stop - start).count();
		timings.push_back(milliseconds);
		printRow(graph, variant, options.workers, iteration, milliseconds, last);
		matches = matches && last.resultHash == expected.resultHash &&
			last.diagnosticHash == expected.diagnosticHash;
	}
	printSummary(graph, variant, options.workers, timings, last);
	if (!matches) {
		std::cerr << "ERROR workload=" << graph.name << " variant=" << variant
			<< " result or diagnostic hash differs from direct reference\n";
	}
	return matches;
}

} // namespace query_benchmark

int main(int argc, char** argv) {
	using namespace query_benchmark;
	const Options options = parseOptions(argc, argv);
	std::vector<Graph> graphs = makeGraphs();
	for (Graph& graph : graphs) {
		for (Query& query : graph.queries) {
			const std::uint64_t scaled = static_cast<std::uint64_t>(query.work) * options.workMultiplier;
			query.work = static_cast<std::uint32_t>(std::min<std::uint64_t>(scaled,
				std::numeric_limits<std::uint32_t>::max()));
		}
	}
	bool allHashesMatch = true;
	for (const Graph& graph : graphs) {
		if (!selected(options.workload, graph.name)) {
			continue;
		}
		const Metrics expected = execute(graph, "direct", options.workers, options.seed ^ graph.seed);
		if (selected(options.variant, "direct")) {
			allHashesMatch = runVariant(graph, "direct", options, expected) && allHashesMatch;
		}
		if (selected(options.variant, "worklist")) {
			allHashesMatch = runVariant(graph, "worklist", options, expected) && allHashesMatch;
		}
		if (selected(options.variant, "coroutine")) {
			allHashesMatch = runVariant(graph, "coroutine", options, expected) && allHashesMatch;
		}
	}
	return allHashesMatch ? 0 : 2;
}
