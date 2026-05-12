template <typename T, int OuterV, int OuterW>
struct Outer {
	template <int InnerV>
	struct Inner {
		static constexpr int bias = static_cast<int>(sizeof(T)) + OuterV + OuterW + InnerV;
		int compute() const {
			return static_cast<int>(sizeof(T)) + InnerV;
		}
	};
};

int main() {
	using Target = Outer<long long, 3, 4>::Inner<5>;
	Target value;
	return (Target::bias + value.compute()) - 33;
}
