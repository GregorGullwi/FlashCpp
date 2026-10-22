struct Record {
	long long wide;
	short narrow;
};

template<class Input> using NoThrow = long (*)(Input) noexcept;
template<class Value> using Forwarded = NoThrow<Value>;
template<class Element> using Nested = Forwarded<Element>;
template<class ArgumentType> using Throwing = long (*)(ArgumentType);
template<class RecordInput> using RecordNoThrow = Record (*)(RecordInput) noexcept;
template<class RecordValue> using RecordThrowing = Record (*)(RecordValue);

template<class Argument>
struct Source {
	operator Nested<Argument>() const { return nullptr; }
};

template<class Field>
struct RecordSource {
	operator RecordNoThrow<Field>() const { return nullptr; }
};

struct ThrowingSource {
	operator Throwing<int>() const { return nullptr; }
};

struct ExactSelection { char value; };
struct RelaxedSelection { char value[2]; };
struct RejectedSelection { long long value; };

ExactSelection rank(NoThrow<int>) { return {1}; }
RelaxedSelection rank(Throwing<int>) { return {{2, 3}}; }
RejectedSelection rank(...) { return {4}; }

RelaxedSelection relax(Throwing<int>) { return {{5, 6}}; }
RejectedSelection relax(...) { return {7}; }
RelaxedSelection relaxRecord(RecordThrowing<short>) { return {{8, 9}}; }
RejectedSelection relaxRecord(...) { return {10}; }
ExactSelection requireNoThrow(NoThrow<int>) { return {11}; }
RejectedSelection requireNoThrow(...) { return {12}; }


static_assert(sizeof(decltype(rank(Source<int>{}))) == sizeof(ExactSelection));
static_assert(sizeof(decltype(relax(Source<int>{}))) == sizeof(RelaxedSelection));
static_assert(sizeof(decltype(relaxRecord(RecordSource<short>{}))) == sizeof(RelaxedSelection));
static_assert(sizeof(decltype(relax(Source<short>{}))) == sizeof(RejectedSelection));
static_assert(sizeof(decltype(relax(RecordSource<int>{}))) == sizeof(RejectedSelection));
static_assert(sizeof(decltype(requireNoThrow(ThrowingSource{}))) == sizeof(RejectedSelection));

int main() {
	return sizeof(decltype(rank(Source<int>{}))) == sizeof(ExactSelection) ? 0 : 1;
}
