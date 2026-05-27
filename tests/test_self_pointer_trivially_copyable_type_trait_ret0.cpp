// Regression: __is_trivially_copyable / __is_trivial on a self-referential pointer-member
// struct must not cause infinite recursion in the type-trait evaluator.
// Pointer members are scalar indirections; their pointee type is not examined.
struct Node {
	Node* next;
};

static_assert(__is_trivially_copyable(Node), "Node with pointer self-ref must be trivially copyable");
static_assert(__is_trivial(Node), "Node with pointer self-ref must be trivial");

int main() {
	return 0;
}
