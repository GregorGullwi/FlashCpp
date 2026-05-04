// Test pack expansion in base class list (e.g., struct Derived : Base<Args>...)
// This pattern is used in std::variant for _Variant_hash_base
template <int I, typename T>
struct BaseDedup : T {};

struct PoisonHash {
	int value;
};

// Use index_sequence-style to test pack expansion bases
template <int... Indices>
struct MyHash : BaseDedup<Indices, PoisonHash>... {
};

int main() {
	MyHash<0, 1, 2> h;
	auto& second_base = static_cast<BaseDedup<1, PoisonHash>&>(h);
	second_base.value = 42;
	return second_base.value - 42;
}
