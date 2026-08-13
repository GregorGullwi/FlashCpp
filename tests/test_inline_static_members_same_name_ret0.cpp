// C++20 inline static data-member definitions with the same unqualified name
// belong to distinct class scopes and must not collide at link time.
struct FirstOrdering {
	constexpr explicit FirstOrdering(int value) : value(value) {}
	static const FirstOrdering less;
	int value;
};

struct SecondOrdering {
	constexpr explicit SecondOrdering(int value) : value(value) {}
	static const SecondOrdering less;
	int value;
};

inline constexpr FirstOrdering FirstOrdering::less{-1};
inline constexpr SecondOrdering SecondOrdering::less{-2};

int main() {
	return FirstOrdering::less.value == -1 && SecondOrdering::less.value == -2 ? 0 : 1;
}
