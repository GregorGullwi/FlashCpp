namespace cmp {
using type = signed char;

enum class Ord : type { less = -1, equivalent = 0, greater = 1 };
enum class Ncmp : type { unordered = 2 };

struct unspec {
	constexpr unspec(unspec*) {}
};
}

class partial_ordering {
	int value;

	constexpr explicit partial_ordering(cmp::Ord) : value(0) {}
	constexpr explicit partial_ordering(cmp::Ncmp) : value(0) {}

public:
	static const partial_ordering equivalent;

	friend constexpr partial_ordering operator<=>(cmp::unspec, partial_ordering v) {
		if (v.value & 1) {
			return partial_ordering(cmp::Ord(-v.value));
		}
		return v;
	}

	friend constexpr bool operator==(partial_ordering v, cmp::unspec) {
		return v.value == 0;
	}
};

const partial_ordering partial_ordering::equivalent(cmp::Ord::equivalent);

int main() {
	partial_ordering value = partial_ordering::equivalent;
	cmp::unspec marker(nullptr);
	partial_ordering result = marker <=> value;
	return (result == marker) ? 0 : 1;
}
