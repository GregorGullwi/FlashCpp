// Test: __is_final built-in type trait with std-library-style template wrapper
// Covers direct builtin use, struct-template wrapper (std::is_final pattern),
// variable template shorthand, and static constexpr variable assignment.

// --- std-style infrastructure ---

template<bool B>
struct bool_constant {
	static constexpr bool value = B;
};

using true_type  = bool_constant<true>;
using false_type = bool_constant<false>;

// Mirrors std::is_final: struct deriving from bool_constant<__is_final(T)>
template<typename T>
struct is_final : bool_constant<__is_final(T)> {};

// Variable template shorthand, like std::is_final_v
template<typename T>
inline constexpr bool is_final_v = is_final<T>::value;

// --- Test types ---

class RegularClass {
	int x;
};

class FinalClass final {
	int y;
};

struct RegularStruct {
	int a;
};

struct FinalStruct final {
	int b;
};

// --- Direct builtin checks ---

static_assert(__is_final(FinalClass),    "__is_final(FinalClass) should be true");
static_assert(!__is_final(RegularClass), "!__is_final(RegularClass) should be true");

// --- Template wrapper: ::value member ---

static_assert(is_final<FinalClass>::value,     "is_final<FinalClass>::value should be true");
static_assert(!is_final<RegularClass>::value,  "is_final<RegularClass>::value should be false");
static_assert(is_final<FinalStruct>::value,    "is_final<FinalStruct>::value should be true");
static_assert(!is_final<RegularStruct>::value, "is_final<RegularStruct>::value should be false");

// --- Variable template shorthand ---

static_assert(is_final_v<FinalClass>,    "is_final_v<FinalClass> should be true");
static_assert(!is_final_v<RegularClass>, "is_final_v<RegularClass> should be false");

// --- Static constexpr variable assignment pattern (caching the trait result) ---

static constexpr bool kFinalClassIsFinal   = is_final<FinalClass>::value;
static constexpr bool kRegularClassIsFinal = is_final<RegularClass>::value;

static_assert(kFinalClassIsFinal,    "cached is_final for FinalClass should be true");
static_assert(!kRegularClassIsFinal, "cached is_final for RegularClass should be false");

// --- Primitive types are never final ---

static_assert(!is_final<int>::value, "is_final<int> should be false");
static_assert(!is_final_v<int>,      "is_final_v<int> should be false");

int main() {
	return 0;
}
