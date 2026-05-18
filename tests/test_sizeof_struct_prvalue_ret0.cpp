struct S {
	int x;
};

struct Mixed {
	char c;
	long long value;
};

template <typename T>
struct Box {
	T value;
};

static_assert(sizeof(S{0}) == sizeof(S));
static_assert(sizeof(Mixed{'a', 7}) == sizeof(Mixed));
static_assert(sizeof(Box<char>{'a'}) == sizeof(Box<char>));
static_assert(sizeof(Box<long long>{42}) == sizeof(Box<long long>));

int main() {
	return (sizeof(S{0}) == sizeof(S) &&
			sizeof(Mixed{'a', 7}) == sizeof(Mixed) &&
			sizeof(Box<char>{'a'}) == sizeof(Box<char>) &&
			sizeof(Box<long long>{42}) == sizeof(Box<long long>))
		? 0
		: 1;
}
