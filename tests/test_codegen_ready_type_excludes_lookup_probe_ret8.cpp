// Regression: semantic readiness for constructor codegen must distinguish a
// runtime-reachable nested instantiation from lookup-only registry artifacts.
template <typename T>
struct LookupOnly {
	struct Artifact {
		int value = static_cast<int>(sizeof(T));
	};

	using type = Artifact;
};

template <typename T>
using ProbedType = typename LookupOnly<T>::type;

template <typename T>
auto probe(int) -> ProbedType<T>;

template <typename>
auto probe(...) -> void;

using ProbeResult = decltype(probe<long>(0));
static_assert(sizeof(ProbeResult) == sizeof(int));

template <typename T>
struct Reachable {
	struct Inner {
		int value = static_cast<int>(sizeof(T));
	};
};

int main() {
	Reachable<double>::Inner value;
	return value.value;
}
