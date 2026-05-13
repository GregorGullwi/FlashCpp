// Qualified-id template arguments are classified against the target parameter:
// ns::Item is a type argument, while ns::Owner::Item is a static data member NTTP.

namespace ns {
struct Item {};

struct Owner {
	static constexpr int Item = 9;
};
}

template <typename T>
struct TypeSlot {
	static constexpr int value = sizeof(T);
};

template <>
struct TypeSlot<ns::Item> {
	static constexpr int value = 13;
};

template <int V>
struct ValueSlot {
	static constexpr int value = V;
};

int main() {
	if (TypeSlot<ns::Item>::value != 13) return 1;
	if (ValueSlot<ns::Owner::Item>::value != 9) return 2;
	return 0;
}

