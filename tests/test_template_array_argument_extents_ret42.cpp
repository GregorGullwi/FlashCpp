// An explicit array template argument keeps its whole array shape: the known
// extents survive both a function-template body and a class-template static
// member, while a requires-expression observes the same shape. The direct
// type-id forms must report the same sizes.
template <typename T>
constexpr int size_of() { return static_cast<int>(sizeof(T)); }

template <typename T>
concept HasSize = requires { sizeof(T); };

template <typename T>
constexpr bool sized_in_definition() { return HasSize<T>; }

template <typename T>
struct Probe {
	static constexpr int value = static_cast<int>(sizeof(T));
};

template <typename T>
struct SizedProbe {
	static constexpr bool sized_in_class = HasSize<T>;
};

using KnownRow = int[2][3];
using UnknownRow = long long[][3];

static_assert(size_of<int[5]>() == 20);
static_assert(size_of<int[2][3]>() == 24);
static_assert(size_of<long long[3]>() == 24);
static_assert(size_of<KnownRow>() == 24);
static_assert(!HasSize<UnknownRow>);
static_assert(Probe<int[5]>::value == 20);
static_assert(Probe<int[2][3]>::value == 24);
static_assert(Probe<long long[3]>::value == 24);
static_assert(sizeof(int[5]) == 20);
static_assert(sizeof(int[2][3]) == 24);
static_assert(sizeof(long long[3]) == 24);

static_assert(HasSize<int[5]>);
static_assert(HasSize<int[2][3]>);
static_assert(HasSize<long long[3]>);
static_assert(!HasSize<int[]>);
static_assert(!HasSize<long long[][3]>);
static_assert(sized_in_definition<int[2][3]>());
static_assert(!sized_in_definition<long long[][3]>());
static_assert(SizedProbe<int[2][3]>::sized_in_class);
static_assert(!SizedProbe<int[]>::sized_in_class);
static_assert(!SizedProbe<long long[][3]>::sized_in_class);

int main() {
	int result = 0;
	if (size_of<int[5]>() != 20) {
		result |= 1;
	}
	if (size_of<int[2][3]>() != 24) {
		result |= 2;
	}
	if (size_of<long long[3]>() != 24) {
		result |= 4;
	}
	if (Probe<int[5]>::value != 20) {
		result |= 8;
	}
	if (Probe<int[2][3]>::value != 24) {
		result |= 16;
	}
	return result == 0 ? 42 : 1;
}
