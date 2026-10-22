struct Payload { long long value; short tag; };
template<class T> using Parameter = T;
template<class R, int N> using Callback = R (*)(Parameter<R>);
template<class R, int N = 7> using Defaulted = R (*)(Parameter<R>);
template<int N, class R, bool B, class A> using Interleaved = R (*)(Parameter<A>) noexcept;
template<class A, int N, class R> using Forwarded = Interleaved<N, R, true, A>;
template<int N, class A, class R> using Chained = Forwarded<A, N, R>;

static_assert(__is_same(Callback<short, 7>, short (*)(short)));
static_assert(__is_same(Callback<Payload, 3>, Payload (*)(Payload)));
static_assert(__is_same(Defaulted<long>, long (*)(long)));
static_assert(__is_same(Interleaved<5, long, false, short>, long (*)(short) noexcept));
static_assert(__is_same(Interleaved<9, Payload, true, char>, Payload (*)(char) noexcept));
static_assert(__is_same(Chained<2, short, long>, long (*)(short) noexcept));
static_assert(__is_same(Chained<4, Payload, void>, void (*)(Payload) noexcept));
static_assert(!__is_same(Chained<2, char, long>, Chained<2, short, long>));

long increment(short value) noexcept { return value + 1; }
struct Source {
	operator Chained<7, short, long>() const { return &increment; }
};
long invoke(Chained<7, short, long> callback) { return callback(6); }
int main() {
	Chained<7, short, long> callback = &increment;
	Source source;
	return callback(6) == 7 && invoke(source) == 7 && __is_same(Callback<short, 7>, short (*)(short)) ? 0 : 1;
}
