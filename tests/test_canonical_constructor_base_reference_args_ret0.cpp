// Constructor schemas rank direct base-reference bindings by canonical TypeId.
struct Base { long long value; };
struct Derived : Base { short extra; };
struct PrivateDerived : private Base {};
struct Left : Base {};
struct Right : Base {};
struct Ambiguous : Left, Right {};
struct VirtualDerived : virtual Base {};

struct Receiver {
	Receiver(const Base& argument) noexcept : value(argument.value) {}
	long long value;
};

struct RankedReceiver {
	RankedReceiver(const Base&) noexcept(false) {}
	RankedReceiver(const Derived&) noexcept {}
};

struct MutableReceiver { MutableReceiver(Base&) noexcept {} };
struct RvalueReceiver { RvalueReceiver(Base&&) noexcept {} };

static_assert(__is_constructible(Receiver, Derived&));
static_assert(__is_nothrow_constructible(Receiver, Derived&));
static_assert(__is_nothrow_constructible(Receiver, const Derived&));
static_assert(__is_nothrow_constructible(Receiver, VirtualDerived&));
static_assert(!__is_constructible(Receiver, PrivateDerived&));
static_assert(!__is_constructible(Receiver, Ambiguous&));
static_assert(__is_constructible(MutableReceiver, Derived&));
static_assert(!__is_constructible(MutableReceiver, const Derived&));
static_assert(!__is_constructible(MutableReceiver, Derived));
static_assert(__is_constructible(RvalueReceiver, Derived&&));
static_assert(!__is_constructible(RvalueReceiver, Derived&));
static_assert(!__is_constructible(RvalueReceiver, const Derived&&));
static_assert(__is_nothrow_constructible(RankedReceiver, Derived&));

template<class T> struct TemplateBase { T value; };
template<class T> struct TemplateDerived : TemplateBase<T> { char tag; };
template<class T> struct TemplateReceiver { TemplateReceiver(const TemplateBase<T>&) noexcept {} };
static_assert(__is_constructible(TemplateReceiver<double>, TemplateDerived<double>&));
static_assert(__is_nothrow_constructible(TemplateReceiver<double>, TemplateDerived<double>&));

template<class T> concept Accepted = __is_nothrow_constructible(Receiver, T&);
template<class T> requires Accepted<T> int select(T&) { return 42; }
int select(...) { return 1; }

int main() {
	Derived argument{};
	argument.value = 42;
	Receiver receiver(argument);
	return receiver.value == 42 && select(argument) == 42 ? 0 : 1;
}
