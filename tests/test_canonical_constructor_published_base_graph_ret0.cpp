template<class T> struct Base { T value; };
template<class T> struct Derived : Base<T> { short tag; };
template<class T> struct Partial;
template<class T> struct Partial<T*> : Derived<T> {};
struct Private : private Base<long long> {};
struct Left : Base<long long> {};
struct Right : Base<long long> {};
struct Ambiguous : Left, Right {};
struct Virtual : virtual Base<long long> {};

struct Receiver {
	Receiver(const Base<long long>& argument) noexcept : value(argument.value) {}
	long long value;
};
struct DirectReceiver {
	DirectReceiver(const Partial<long long*>& argument) noexcept : value(argument.value + 1) {}
	long long value;
};
struct ExplicitReceiver { explicit ExplicitReceiver(const Partial<long long*>&) {} };

struct Accepted { char marker[3]; };
struct Rejected { char marker[7]; };
Accepted receive(Receiver);
Rejected receive(...);
Accepted direct(DirectReceiver);
Rejected direct(...);
Accepted explicit_only(ExplicitReceiver);
Rejected explicit_only(...);

static_assert(__is_constructible(Receiver, Partial<long long*>&));
static_assert(__is_nothrow_constructible(Receiver, Virtual&));
static_assert(!__is_constructible(Receiver, Private&));
static_assert(!__is_constructible(Receiver, Ambiguous&));
static_assert(!__is_constructible(Receiver, Derived<short>&));
static_assert(sizeof(decltype(receive(Partial<long long*>{}))) == sizeof(Accepted));
static_assert(sizeof(decltype(direct(Partial<long long*>{}))) == sizeof(Accepted));
static_assert(sizeof(decltype(explicit_only(Partial<long long*>{}))) == sizeof(Rejected));

int main() {
	Partial<long long*> source{};
	source.value = 41;
	Receiver receiver(source);
	DirectReceiver direct_receiver(source);
	return receiver.value == 41 && direct_receiver.value == 42 ? 0 : 1;
}
