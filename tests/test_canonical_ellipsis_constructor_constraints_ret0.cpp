struct Packet {
	Packet(short, ...) noexcept {}
};

struct Defaulted {
	Defaulted(int = 3, ...) noexcept {}
};

struct Inherited : Packet {
	using Packet::Packet;
};

enum class Tag : unsigned char { Value };
using Callback = long(double);

template<typename T>
concept FromExtraArguments = __is_constructible(T, short, Tag, int*, int(&)[3], Callback&);

template<typename T>
concept NothrowFromExtraArguments = __is_nothrow_constructible(T, short, double, char);

template<typename T> requires FromExtraArguments<T> && NothrowFromExtraArguments<T>
int select(T*) {
	return 1;
}

int select(...) {
	return 0;
}

static_assert(__is_constructible(Defaulted));
static_assert(__is_constructible(Defaulted, int, double));
static_assert(__is_nothrow_constructible(Defaulted, int, double));
static_assert(!__is_trivially_constructible(Packet, short, double));
static_assert(!__is_constructible(Packet, short, void));
static_assert(__is_constructible(Inherited, short, double));

int main() {
	Packet* packet = nullptr;
	Defaulted* defaulted = nullptr;
	return select(packet) == 1 && select(defaulted) == 1 ? 0 : 1;
}
