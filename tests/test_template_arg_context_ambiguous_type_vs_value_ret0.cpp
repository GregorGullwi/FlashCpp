// Parameter-context classification must choose the type name for a type parameter,
// even when an ordinary constexpr object with the same spelling is visible.

struct Ambiguous {};
constexpr int Ambiguous = 7;

template <typename T>
struct TypeSlot {
	static constexpr int value = 11;
};

template <>
struct TypeSlot<struct Ambiguous> {
	static constexpr int value = 23;
};

template <int V>
struct ValueSlot {
	static constexpr int value = V;
};

int main() {
	if (TypeSlot<struct Ambiguous>::value != 23) return 1;
	if (ValueSlot<Ambiguous>::value != 7) return 2;
	return 0;
}
