template<class T> struct Base { T value; };
template<class T> struct Derived : Base<T> { short tag; };
template<class T> struct Partial;
template<class T> struct Partial<T*> : Derived<T> {};
struct Virtual : virtual Base<long long> {};
struct Private : private Base<long long> {};
struct Left : Base<long long> {};
struct Right : Base<long long> {};
struct Ambiguous : Left, Right {};
using BaseAlias = Base<long long>;
using ChainedBase = BaseAlias;
using OwnerAlias = Partial<long long*>;

template<class T> struct Constructed { Constructed(const Derived<T>&) {} };
struct Standard { char marker[3]; };
struct UserDefined { char marker[7]; };
struct Rejected { char marker[11]; };
Standard choose(ChainedBase);
UserDefined choose(Constructed<long long>);
Rejected choose(...);
Standard unrelated(Base<short>);
Rejected unrelated(...);
static_assert(sizeof(decltype(choose(OwnerAlias{}))) == sizeof(Standard));
static_assert(sizeof(decltype(choose(Derived<long long>{}))) == sizeof(Standard));
static_assert(sizeof(decltype(choose(Derived<long long>()))) == sizeof(Standard));
static_assert(sizeof(decltype(choose(Virtual{}))) == sizeof(Standard));
static_assert(sizeof(decltype(unrelated(OwnerAlias{}))) == sizeof(Rejected));
static_assert(!__is_constructible(BaseAlias, Private&));
static_assert(!__is_constructible(BaseAlias, Ambiguous&));

struct Input { short value; };
struct Receiver {
	Receiver(const Input& input) : value(input.value) {}
	long long value;
};
template<class T> struct ExplicitReceiver {
	explicit ExplicitReceiver(const Input& input) : value(input.value) {}
	T value;
};
Standard implicit_explicit(ExplicitReceiver<long long>);
Rejected implicit_explicit(...);
static_assert(sizeof(decltype(implicit_explicit(Input{}))) == sizeof(Rejected));
static_assert(sizeof(decltype(implicit_explicit(ExplicitReceiver<long long>(Input{})))) == sizeof(Standard));
template<class Marker> Standard explicit_outer(ExplicitReceiver<long long>);
template<class Marker> Rejected explicit_outer(...);
static_assert(sizeof(decltype(explicit_outer<short>(ExplicitReceiver<long long>(Input{})))) == sizeof(Standard));
template<class T> struct ExplicitPartial;
template<class T> struct ExplicitPartial<T*> {
	explicit ExplicitPartial(const Input& input) : value(input.value) {}
	T value;
};
Standard implicit_partial(ExplicitPartial<short*>);
Rejected implicit_partial(...);
static_assert(sizeof(decltype(implicit_partial(Input{}))) == sizeof(Rejected));
struct OperatorSource {
	operator BaseAlias() const {
		BaseAlias result{};
		result.value = 37;
		return result;
	}
};
long long read_constructor(Receiver object) { return object.value; }
long long read_operator(BaseAlias object) { return object.value; }

int main() {
	Input input{9};
	ExplicitReceiver<long long> direct(input);
	ExplicitReceiver<long long> direct_list{input};
	ExplicitPartial<short*> partial(input);
	return direct.value == 9 && direct_list.value == 9 && partial.value == 9 && read_constructor(input) == 9 && read_operator(OperatorSource{}) == 37 &&
		sizeof(decltype(choose(OwnerAlias{}))) == sizeof(Standard) ? 0 : 1;
}
