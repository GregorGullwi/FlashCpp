// MSVC <cmath> calls compiler intrinsics such as __ceilf / __floor / __truncf
// rather than the C library wrappers when _HAS_CMATH_INTRINSICS is set.
// These names must be visible as builtins so header inclusion can bind them.

using CeilfResult = decltype(__ceilf(*(float*)0));
using FloorResult = decltype(__floor(*(double*)0));
using TruncfResult = decltype(__truncf(*(float*)0));
using CopysignfResult = decltype(__copysignf(*(float*)0, *(float*)0));

template <typename A, typename B>
struct same_type {
	static constexpr bool value = false;
};

template <typename A>
struct same_type<A, A> {
	static constexpr bool value = true;
};

static_assert(same_type<CeilfResult, float>::value);
static_assert(same_type<FloorResult, double>::value);
static_assert(same_type<TruncfResult, float>::value);
static_assert(same_type<CopysignfResult, float>::value);

int main() {
	return 0;
}
