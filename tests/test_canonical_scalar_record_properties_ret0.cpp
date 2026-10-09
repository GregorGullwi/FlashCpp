// Scalar record-property traits are determined by canonical type shape, while
// records continue to use their published canonical facts.
struct ScalarPropertyRecord {
	int value;
	void method();
};

template <typename T>
struct ScalarPropertyTemplate {
	T value;
};

enum class ScalarPropertyEnum : unsigned short {
	Value = 1,
};

using ScalarPropertyMemberObjectPointer = int ScalarPropertyRecord::*;
using ScalarPropertyMemberFunctionPointer = void (ScalarPropertyRecord::*)();

template <typename T>
concept ScalarPropertyPositiveFacts =
	__is_trivially_copyable(T) && __is_trivial(T) && __is_pod(T) &&
	__is_standard_layout(T) && __is_destructible(T) && __is_trivially_destructible(T) &&
	__is_nothrow_destructible(T) && __has_trivial_destructor(T);

template <typename T>
concept ScalarPropertyNegativeFacts = !__is_empty(T) && !__is_aggregate(T) && !__has_virtual_destructor(T);

template <typename T>
concept HasScalarPropertyFacts = ScalarPropertyPositiveFacts<T> && ScalarPropertyNegativeFacts<T>;

static_assert(HasScalarPropertyFacts<int>);
static_assert(HasScalarPropertyFacts<double>);
static_assert(HasScalarPropertyFacts<int* const>);
static_assert(HasScalarPropertyFacts<ScalarPropertyEnum>);
static_assert(HasScalarPropertyFacts<decltype(nullptr)>);
static_assert(HasScalarPropertyFacts<ScalarPropertyMemberObjectPointer>);
static_assert(HasScalarPropertyFacts<ScalarPropertyMemberFunctionPointer>);

static_assert(__is_trivially_copyable(ScalarPropertyRecord));
static_assert(__is_aggregate(ScalarPropertyRecord));
static_assert(!__is_empty(ScalarPropertyRecord));
static_assert(__is_trivially_copyable(ScalarPropertyTemplate<int>));
static_assert(__is_aggregate(ScalarPropertyTemplate<unsigned char>));

int main() {
	int mismatches = 0;
	mismatches += !__is_trivially_copyable(int);
	mismatches += !__is_trivial(int);
	mismatches += !__is_pod(int);
	mismatches += !__is_standard_layout(int);
	mismatches += !__is_destructible(int);
	mismatches += !__is_trivially_destructible(int);
	mismatches += !__is_nothrow_destructible(int);
	mismatches += !__has_trivial_destructor(int);
	mismatches += __is_empty(int);
	mismatches += __is_aggregate(int);
	mismatches += __has_virtual_destructor(int);
	mismatches += !__is_trivially_copyable(ScalarPropertyEnum);
	mismatches += !__is_standard_layout(ScalarPropertyEnum);
	mismatches += __is_empty(ScalarPropertyEnum);
	mismatches += __is_aggregate(ScalarPropertyEnum);
	mismatches += __has_virtual_destructor(ScalarPropertyEnum);
	mismatches += !__is_trivially_copyable(int* const);
	mismatches += !__is_standard_layout(int* const);
	mismatches += __is_aggregate(int* const);
	mismatches += !__is_trivially_copyable(ScalarPropertyMemberObjectPointer);
	mismatches += !__is_standard_layout(ScalarPropertyMemberObjectPointer);
	mismatches += __is_aggregate(ScalarPropertyMemberObjectPointer);
	mismatches += !__is_trivially_copyable(ScalarPropertyMemberFunctionPointer);
	mismatches += !__is_standard_layout(ScalarPropertyMemberFunctionPointer);
	mismatches += __has_virtual_destructor(ScalarPropertyMemberFunctionPointer);
	mismatches += !__is_trivially_copyable(ScalarPropertyRecord);
	mismatches += !__is_aggregate(ScalarPropertyRecord);
	mismatches += __is_empty(ScalarPropertyRecord);
	mismatches += !__is_trivially_copyable(ScalarPropertyTemplate<int>);
	mismatches += !__is_aggregate(ScalarPropertyTemplate<unsigned char>);
	return mismatches;
}
