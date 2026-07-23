template <typename Value>
struct TransformedCallback {
	Value* (*function)(const Value&, Value&&);
};

struct ConcreteCallback {
	int* (*function)(const int&, int&&);
};

struct DifferentCallback {
	int* (*function)(int&, const int&&);
};

static_assert(__is_same(
	decltype(TransformedCallback<int>::function),
	decltype(ConcreteCallback::function)));
static_assert(!__is_same(
	decltype(TransformedCallback<int>::function),
	decltype(DifferentCallback::function)));

int main() {
	return __is_same(
			   decltype(TransformedCallback<int>::function),
			   decltype(ConcreteCallback::function)) &&
			   !__is_same(
				   decltype(TransformedCallback<int>::function),
				   decltype(DifferentCallback::function))
		? 0
		: 1;
}
