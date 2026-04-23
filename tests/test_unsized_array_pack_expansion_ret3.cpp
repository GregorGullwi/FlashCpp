// Phase 6 regression: unsized array with pack-expanded initializer.
// Before the fix, `int arr[] = {args...}` kept the parser-time pre-expansion
// size of 1 (the single PackExpansionExprNode).  After the fix, the outer
// dimension is re-inferred post-substitution to match the expanded element
// count.  Returns sizeof(arr)/sizeof(arr[0]) = 3 so a regression of the bug
// would return 1.
template<typename... Ts>
int count_elements(Ts... args) {
	int arr[] = {static_cast<int>(args)...};
	return (int)(sizeof(arr) / sizeof(arr[0]));
}

int main() {
	return count_elements(10, 20, 30);
}
