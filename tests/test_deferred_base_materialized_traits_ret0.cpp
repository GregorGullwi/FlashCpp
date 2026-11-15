// Materialized records must still answer the record-property traits; the new
// deferred-base guard only applies when a base is actually unresolved.

struct EmptyBase {};
struct TrivialBase {};

struct EmptyDerived : EmptyBase {};
struct TrivialDerived : TrivialBase {};

static_assert(__is_empty(EmptyDerived));
static_assert(__is_trivially_destructible(TrivialDerived));
static_assert(__is_nothrow_destructible(TrivialDerived));

int main() {
	return 0;
}
