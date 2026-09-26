// Sema conversion annotation must plan projectable function decay by TypeId,
// including the full noexcept function type.
int increment(int value) noexcept {
	return value + 1;
}

int main() {
	int (*noexcept_pointer)(int) noexcept = increment;
	int (*function_pointer)(int) = noexcept_pointer;
	return function_pointer(41);
}
