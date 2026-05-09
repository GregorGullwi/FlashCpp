// Regression: lazy member function body reparse and lazy static member replay
// must preserve outer + inner template environments.

template <typename T, int N>
struct Outer {
	template <int M>
	struct Inner {
		static constexpr int bias = static_cast<int>(sizeof(T)) + N + M;
		int compute() const { return static_cast<int>(sizeof(T)) + M; }
	};
};

int main() {
	using Target = Outer<long long, 3>::Inner<5>;
	Target value;
	return (Target::bias + value.compute()) - 29;
}
