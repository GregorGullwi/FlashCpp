// Test that constexpr specifier before struct definition propagates to
// trailing variable declarations.

struct Val {
	int v;
};

constexpr Val answer = {42};

int main() {
	return answer.v;	 // expect 42
}
