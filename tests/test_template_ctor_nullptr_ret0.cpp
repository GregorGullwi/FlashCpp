// Regression test: materializeMatchingConstructorTemplate should return nullptr
// (not the uninstantiated template) when try_instantiate_constructor_template
// fails, so callers can fall through to arity-based resolution instead of
// silently using the uninstantiated template.
//
// This tests the case where a struct has both a template constructor and a
// concrete constructor.  When called with concrete args, the concrete ctor
// should be selected rather than producing a noop via the uninstantiated
// template.

struct Pair {
int x;
int y;

// Concrete constructor
Pair(int a, int b) : x(a), y(b) {}

// Template constructor that only accepts char pointers (constrained via type)
template<typename T>
Pair(T* /*ptr_unused*/) : x(-1), y(-1) {}
};

int main() {
Pair p(10, 20);
if (p.x != 10) return 1;
if (p.y != 20) return 2;
return 0;
}
