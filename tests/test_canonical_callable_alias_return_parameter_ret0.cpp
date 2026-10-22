struct Wide { long long value; short tag; };
template<class Content> using Parameter = Content;
template<class Result> using Function = Result(short);
template<class Output> using SafePointer = Output (*)(int) noexcept;
template<class Referent> using Reference = Referent (&)(short);
template<class Target> using RvalueReference = Target (&&)(short);
template<class Return, class Argument> using Callback = Return (*)(Argument) noexcept;
template<class ReturnValue, class ArgumentValue> using ComponentCallback = ReturnValue (*)(Parameter<ArgumentValue>) noexcept;
template<class R, class A> using QualifiedCallback = R (*)(Parameter<const A*>) noexcept;
template<class Value, class Input> using Forwarded = Callback<Value, Input>;
template<class Yield, class Element> using Nested = Forwarded<Yield, Element>;
template<class Entry> using Row = Entry (*[2])(short);

static_assert(__is_same(Function<long>, long(short)));
static_assert(__is_same(Function<Wide>, Wide(short)));
static_assert(__is_same(SafePointer<long>, long (*)(int) noexcept));
static_assert(__is_same(SafePointer<Wide>, Wide (*)(int) noexcept));
static_assert(__is_same(Reference<long>, long (&)(short)));
static_assert(__is_same(RvalueReference<Wide>, Wide (&&)(short)));
static_assert(__is_same(Callback<long*, short>, long* (*)(short) noexcept));
static_assert(__is_same(Callback<void, Wide>, void (*)(Wide) noexcept));
static_assert(__is_same(Callback<long, char>, long (*)(char) noexcept));
static_assert(__is_same(ComponentCallback<long, short>, long (*)(short) noexcept));
static_assert(__is_same(ComponentCallback<Wide, int>, Wide (*)(int) noexcept));
static_assert(__is_same(QualifiedCallback<long, short>, long (*)(const short*) noexcept));
static_assert(__is_same(Nested<Wide, short>, Wide (*)(short) noexcept));
static_assert(__is_same(Row<long>, long (*[2])(short)));
static_assert(__is_constructible(Function<long>&&, Function<long>&));
static_assert(!__is_same(SafePointer<long>, SafePointer<short>));

long increment(short value) noexcept { return value + 1; }

int main() {
	ComponentCallback<long, short> callback = &increment;
	return callback(6) == 7 && __is_same(Nested<Wide, short>, Wide (*)(short) noexcept) ? 0 : 1;
}
