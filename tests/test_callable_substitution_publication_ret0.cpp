struct Payload { long long value; short tag; };
template<class Result, class Argument> using Callback = Result (*)(Argument);
template<class Result, class... Arguments> struct PackedCallback { Result (*value)(Arguments...); };
template<class Left, class Right> struct Same { static constexpr bool value = false; };
template<class Type> struct Same<Type, Type> { static constexpr bool value = true; };

short narrow(char value) { return value + 3; }
double scale(short value) { return value * 1.5; }
Payload transform(Payload value) { value.value += value.tag; return value; }
long long empty() { return 5000000000LL; }
long long combine(char first, short second, long long third) { return first + second + third; }

static_assert(Same<Callback<short, char>, short (*)(char)>::value);
static_assert(Same<Callback<Payload, Payload>, Payload (*)(Payload)>::value);
static_assert(__is_same(decltype(PackedCallback<long long>::value), long long (*)()));
static_assert(__is_same(decltype(PackedCallback<long long, char, short, long long>::value), long long (*)(char, short, long long)));

int main() {
	Callback<short, char> small = &narrow;
	Callback<double, short> floating = &scale;
	Callback<Payload, Payload> record = &transform;
	PackedCallback<long long> zero{&empty};
	PackedCallback<long long, char, short, long long> full{&combine};
	Payload input{6000000000LL, 7};
	Payload output = record(input);
	if (small(4) != 7 || floating(6) != 9.0 || output.value != 6000000007LL || output.tag != 7) return 1;
	if (zero.value() != 5000000000LL || full.value(2, 3, 6000000000LL) != 6000000005LL) return 2;
	return 0;
}
