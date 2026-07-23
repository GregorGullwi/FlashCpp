long long combine(short first, int second, long long third) {
	return first + second + third;
}

template <typename Result, typename... Arguments>
struct PackedCallback {
	Result (*callback)(Arguments...);
};

struct ConcreteCallback {
	long long (*callback)(short, int, long long);
};

struct DifferentCallback {
	long long (*callback)(short, long long);
};

static_assert(__is_same(
	decltype(PackedCallback<long long, short, int, long long>::callback),
	decltype(ConcreteCallback::callback)));
static_assert(!__is_same(
	decltype(PackedCallback<long long, short, int, long long>::callback),
	decltype(DifferentCallback::callback)));

int main() {
	PackedCallback<long long, short, int, long long> value{combine};
	if (value.callback(10, 12, 20) != 42) {
		return 1;
	}
	if (!__is_same(
			decltype(PackedCallback<long long, short, int, long long>::callback),
			decltype(ConcreteCallback::callback))) {
		return 2;
	}
	return __is_same(
			   decltype(PackedCallback<long long, short, int, long long>::callback),
			   decltype(DifferentCallback::callback))
		? 3
		: 0;
}
