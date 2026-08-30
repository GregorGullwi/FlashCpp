// Test that arrow member access on a heap-allocated struct with a non-existent
// member name produces a clear "member not found" diagnostic rather than the
// confusing "could not resolve pointed-to object '@new_N'" message.
// This is a negative (fail) test — the compiler must reject it.

struct Pt { int x; int y; };
constexpr int f() {
	Pt* p = new Pt{1, 2};
	int v = p->nonexistent;   // member does not exist
	delete p;
	return v;
}
static_assert(f() == 0);

int main() { return 0; }
