template <typename Value>
struct NestedCallback {
	Value (*function)(Value (*)(const Value&));
};

struct ConcreteCallback {
	int (*function)(int (*)(const int&));
};

struct DifferentCallback {
	int (*function)(int (*)(int&));
};

template <typename Value>
Value (*nestedReturnCallback())(const Value&);

int (*concreteReturnCallback())(const int&);
int (*differentReturnCallback())(int&);

static_assert(__is_same(
	decltype(NestedCallback<int>::function),
	decltype(ConcreteCallback::function)));
static_assert(!__is_same(
	decltype(NestedCallback<int>::function),
	decltype(DifferentCallback::function)));
static_assert(__is_same(
	decltype(nestedReturnCallback<int>),
	decltype(concreteReturnCallback)));
static_assert(!__is_same(
	decltype(nestedReturnCallback<int>),
	decltype(differentReturnCallback)));

int main() {
	return __is_same(
			   decltype(NestedCallback<int>::function),
			   decltype(ConcreteCallback::function)) &&
		   !__is_same(
			   decltype(NestedCallback<int>::function),
			   decltype(DifferentCallback::function)) &&
		   __is_same(
			   decltype(nestedReturnCallback<int>),
			   decltype(concreteReturnCallback)) &&
		   !__is_same(
			   decltype(nestedReturnCallback<int>),
			   decltype(differentReturnCallback))
		? 0
		: 1;
}
