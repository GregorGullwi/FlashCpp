template<class T> struct Base {
	T member;
	T run(short) noexcept;
};
template<class T> struct Primary : Base<T> {};
template<class T> struct Partial;
template<class T> struct Partial<T*> : Primary<T> {
	T get() { return this->member; }
};
template<> struct Partial<int> : Base<short> {};

struct Late;
using EarlyMember = short Late::*;
struct Late : Base<short> {};
using LongBase = Base<long long>;
using ShortBase = Base<short>;
using PartialOwner = Partial<long long*>;
using FullOwner = Partial<int>;
template<class = decltype(Partial<short*>{})> struct ShapeTrigger {};

struct Selected { char marker[3]; };
struct Rejected { char marker[7]; };
Selected partial_data(long long PartialOwner::*);
Rejected partial_data(...);
Selected partial_function(long long (PartialOwner::*)(short));
Rejected partial_function(...);
Selected full_data(short FullOwner::*);
Rejected full_data(...);
Selected late_data(EarlyMember);
Rejected late_data(...);

static_assert(sizeof(decltype(partial_data(&LongBase::member))) == sizeof(Selected));
static_assert(sizeof(decltype(partial_function(&LongBase::run))) == sizeof(Selected));
static_assert(sizeof(decltype(full_data(&ShortBase::member))) == sizeof(Selected));
static_assert(sizeof(decltype(late_data(&ShortBase::member))) == sizeof(Selected));
static_assert(sizeof(decltype(partial_data(&ShortBase::member))) == sizeof(Rejected));

int main() {
	Partial<short*> upgraded{};
	upgraded.member = 9;
	return upgraded.get() == 9 && sizeof(decltype(partial_data(&LongBase::member))) == sizeof(Selected) &&
		sizeof(decltype(full_data(&ShortBase::member))) == sizeof(Selected) ? 0 : 1;
}
