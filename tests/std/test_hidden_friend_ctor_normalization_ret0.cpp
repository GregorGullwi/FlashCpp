namespace cmp {
enum class Type : signed char {
	less = -1,
	equivalent = 0,
	greater = 1
};

enum class Ord : signed char {
	less = -1,
	equivalent = 0,
	greater = 1
};

enum class Ncmp : signed char {
	unordered = 2
};

class PartialOrdering {
	Type value;

	constexpr explicit PartialOrdering(Ord v) noexcept : value(Type(v)) {}
	constexpr explicit PartialOrdering(Ncmp v) noexcept : value(Type(v)) {}

public:
	static const PartialOrdering less;

	friend constexpr PartialOrdering operator<=>(int, PartialOrdering v) noexcept {
		return PartialOrdering(Ord(-1));
	}
};

inline constexpr PartialOrdering PartialOrdering::less(Ord::less);
}

int main() {
	cmp::PartialOrdering value = 0 <=> cmp::PartialOrdering::less;
	return 0;
}
