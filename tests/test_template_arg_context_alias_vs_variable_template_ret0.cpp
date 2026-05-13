// Alias-template template-ids used as type arguments and variable-template
// template-ids used as non-type arguments must be classified by the receiving
// parameter kind, not by parse-time identifier heuristics alone.

template <typename T>
using alias_arg_t = T;

template <typename T>
constexpr int value_arg_v = sizeof(T) + 3;

template <typename T>
struct TypeSlot {
	static constexpr int value = sizeof(T);
};

template <int V>
struct ValueSlot {
	static constexpr int value = V;
};

int main() {
	if (TypeSlot<alias_arg_t<int>>::value != 4) return 1;
	if (ValueSlot<value_arg_v<int>>::value != 7) return 2;
	return 0;
}

