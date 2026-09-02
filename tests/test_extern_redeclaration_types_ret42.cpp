// Namespace-scope extern declarations and definitions are one entity.
// This covers repeated compatible declarations in both declaration orders,
// plus scalar, wide, and aggregate object types.

extern int scalar;
extern int scalar;
int scalar = 17;
extern int scalar;

long long wide = 1000000000000LL;
extern long long wide;
extern long long wide;

struct Pair {
	int first;
	long second;
};

extern Pair pair_value;
Pair pair_value = {3, 4};
extern Pair pair_value;

extern int array_value[];
int array_value[2] = {5, 6};
extern int array_value[2];

static int internal_value = 0;
extern int internal_value;

namespace reopened {
extern int namespace_value;
}
namespace reopened {
int namespace_value = 17;
}
namespace reopened {
extern int namespace_value;
}

template<typename T>
struct Box {
	T value;
};

extern Box<short> templated_value;
Box<short> templated_value = {9};
extern Box<short> templated_value;

int main() {
	if (templated_value.value != 9) return 1;
	return scalar + static_cast<int>(wide / 1000000000000LL) + pair_value.first +
		static_cast<int>(pair_value.second) + reopened::namespace_value + array_value[0] - 5;
}
