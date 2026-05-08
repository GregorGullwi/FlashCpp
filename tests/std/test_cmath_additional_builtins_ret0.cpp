using AcoshfResult = decltype(__builtin_acoshf(*(float*)0));
using CbrtlResult = decltype(__builtin_cbrtl(*(long double*)0));
using FmaResult = decltype(__builtin_fma(*(double*)0, *(double*)0, *(double*)0));
using IlogbResult = decltype(__builtin_ilogbf(*(float*)0));
using LlrintResult = decltype(__builtin_llrintl(*(long double*)0));
using NexttowardResult = decltype(__builtin_nexttowardf(*(float*)0, *(long double*)0));
using RemquoResult = decltype(__builtin_remquo(*(double*)0, *(double*)0, (int*)0));
using ScalblnResult = decltype(__builtin_scalbln(*(double*)0, *(long*)0));

template <typename A, typename B>
struct same_type {
	static constexpr bool value = false;
};

template <typename A>
struct same_type<A, A> {
	static constexpr bool value = true;
};

static_assert(same_type<AcoshfResult, float>::value);
static_assert(same_type<CbrtlResult, long double>::value);
static_assert(same_type<FmaResult, double>::value);
static_assert(same_type<IlogbResult, int>::value);
static_assert(same_type<LlrintResult, long long>::value);
static_assert(same_type<NexttowardResult, float>::value);
static_assert(same_type<RemquoResult, double>::value);
static_assert(same_type<ScalblnResult, double>::value);

int main() {
	return 0;
}
