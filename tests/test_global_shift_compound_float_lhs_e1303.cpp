// C++20 [expr.shift]/1 requires integral or unscoped enum operands.
// The global lowering path must reject a floating-point left operand.
float value = 8.0f;

int main() {
	value <<= 1;
	return static_cast<int>(value);
}
