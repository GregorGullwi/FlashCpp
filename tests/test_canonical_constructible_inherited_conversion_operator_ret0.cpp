// Conversion operators declared on non-virtual bases participate in argument
// conversion ranking. An inaccessible inherited operator makes the conversion
// ill-formed, and equal-ranked operators from separate bases stay ambiguous.

struct BaseInt { operator int() const; };
struct DerivedBaseInt : BaseInt {};
struct PrivateBaseInt { private: operator int() const; };
struct DerivedPrivateBaseInt : PrivateBaseInt {};
struct BaseIntA { operator int() const; };
struct BaseIntB { operator int() const; };
struct DerivedAmbiguous : BaseIntA, BaseIntB {};
struct TargetInt { TargetInt(int); };

static_assert(__is_constructible(TargetInt, DerivedBaseInt));
static_assert(!__is_constructible(TargetInt, DerivedPrivateBaseInt));
static_assert(!__is_constructible(TargetInt, DerivedAmbiguous));

int main() {
	return 0;
}
