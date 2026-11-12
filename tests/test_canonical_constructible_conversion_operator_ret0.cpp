// A class argument can reach a non-class constructor parameter through a direct
// conversion operator. The selected operator decides constructibility, so an
// inaccessible/deleted best operator makes the conversion ill-formed instead of
// deferring to the compatibility resolver, while explicit operators are not
// candidates for implicit argument conversions.

struct PublicInt { operator int() const; };
struct PrivateInt { private: operator int() const; };
struct DeletedInt { operator int() const = delete; };
struct ExplicitInt { explicit operator int() const; };
struct PublicLong { operator long() const; };
struct PrivateExactPublicWorse { private: operator int() const; public: operator long() const; };
struct PublicExactPublicWorse { operator int() const; operator long() const; };
struct TargetInt { TargetInt(int); };
struct TargetConstIntRef { TargetConstIntRef(const int&); };

static_assert(__is_constructible(TargetInt, PublicInt));
static_assert(!__is_constructible(TargetInt, PrivateInt));
static_assert(!__is_constructible(TargetInt, DeletedInt));
static_assert(!__is_constructible(TargetInt, ExplicitInt));
static_assert(__is_constructible(TargetInt, PublicLong));
static_assert(!__is_constructible(TargetInt, PrivateExactPublicWorse));
static_assert(__is_constructible(TargetInt, PublicExactPublicWorse));
static_assert(__is_constructible(TargetConstIntRef, PublicInt));

int main() {
	return 0;
}
