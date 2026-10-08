// A class-template specialization's constructor schema is keyed by its canonical
// TypeId, so an exact argument match is answered from the schema instead of the
// compatibility fallback that reports every user-provided constructor as
// potentially throwing.
template <int Mode>
struct Coded {
	Coded(int) noexcept(Mode == 0) {}
	int tag = 0;
};

static_assert(__is_constructible(Coded<0>, int), "specialization argument is constructible");
static_assert(__is_nothrow_constructible(Coded<0>, int), "mode zero constructor is nothrow");
static_assert(!__is_nothrow_constructible(Coded<1>, int), "non-zero mode constructor can throw");

int main() {
	Coded<0> zero{1};
	Coded<1> one{2};
	return zero.tag == 0 && one.tag == 0 ? 0 : 1;
}
