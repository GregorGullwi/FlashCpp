template<class T> struct Base {
	T value;
	T run(short) noexcept;
};
template<class T> struct Middle : Base<T> {};
template<class T> struct Derived : Middle<T> {};

using LongBase = Base<long long>;
using ShortBase = Base<short>;
using LongDerived = Derived<long long>;
using ShortDerived = Derived<short>;
struct Selected { char marker[3]; };
struct Rejected { char marker[7]; };
Selected data(long long LongDerived::*);
Rejected data(...);
Selected function(long long (LongDerived::*)(short));
Rejected function(...);
Selected short_data(short ShortDerived::*);
Rejected short_data(...);
Selected reverse_data(long long LongBase::*);
Rejected reverse_data(...);

static_assert(sizeof(decltype(data(&LongBase::value))) == sizeof(Selected));
static_assert(sizeof(decltype(function(&LongBase::run))) == sizeof(Selected));
static_assert(sizeof(decltype(short_data(&ShortBase::value))) == sizeof(Selected));
static_assert(sizeof(decltype(data(&ShortBase::value))) == sizeof(Rejected));
static_assert(sizeof(decltype(reverse_data(static_cast<long long LongDerived::*>(nullptr)))) == sizeof(Rejected));

int main() {
	return sizeof(decltype(data(&LongBase::value))) == sizeof(Selected) &&
		sizeof(decltype(function(&LongBase::run))) == sizeof(Selected) ? 0 : 1;
}
