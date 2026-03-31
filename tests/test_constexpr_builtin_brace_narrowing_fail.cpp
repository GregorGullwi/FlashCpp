// C++20 [dcl.init.list]: direct-list-initialization must reject narrowing
// even in constant-evaluated builtin brace construction.

constexpr int value = int{3.14};

int main() {
	return value;
}
