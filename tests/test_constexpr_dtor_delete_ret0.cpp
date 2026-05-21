// Test constexpr destructor invocation via delete expression.
// C++20 [expr.delete]: the destructor is invoked before the memory is freed,
// even in a constant-expression context.

struct Counted {
	int value;
	constexpr explicit Counted(int v) : value(v) {}
	constexpr ~Counted() {}
};

constexpr int test_delete_invokes_dtor() {
	Counted* p = new Counted(42);
	int v = p->value;
	delete p;
	return v;
}
static_assert(test_delete_invokes_dtor() == 42,
	"delete invokes constexpr destructor");

// Struct with heap member freed by its own constexpr destructor
struct Resource {
	int* data;
	constexpr explicit Resource(int v) : data(new int(v)) {}
	constexpr ~Resource() { delete data; }
};

constexpr int test_heap_cleanup_in_dtor() {
	Resource r(123);
	return *r.data;
} // ~Resource() deletes data - all heap freed
static_assert(test_heap_cleanup_in_dtor() == 123,
	"constexpr destructor frees heap allocation");

// delete[] with constexpr destructor (empty body)
struct Tag {
	int id;
	constexpr Tag() : id(0) {}
	constexpr ~Tag() {}
};

constexpr bool test_delete_array_with_dtor() {
	Tag* tags = new Tag[3];
	delete[] tags;
	return true;
}
static_assert(test_delete_array_with_dtor(),
	"delete[] invokes constexpr destructor");

int main() { return 0; }
