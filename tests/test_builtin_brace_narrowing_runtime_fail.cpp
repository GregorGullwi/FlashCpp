// C++20 [dcl.init.list]: direct-list-initialization must reject narrowing.
// Regression test for builtin brace construction in expression context.

int main() {
	return int{3.14};
}
