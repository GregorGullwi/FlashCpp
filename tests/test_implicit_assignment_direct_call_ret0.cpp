// A direct call to an implicitly-declared assignment operator is an ODR use.
// Semantic analysis must publish the selected operator so codegen emits its
// definition, even when no assignment expression spells it.
struct Value {
	int data;
};

int main() {
	Value source;
	source.data = 42;
	Value target;
	target.data = 0;
	target.operator=(source);
	if (target.data != 42)
		return 1;

	Value move_source;
	move_source.data = 24;
	Value move_target;
	move_target.data = 0;
	move_target.operator=(static_cast<Value&&>(move_source));
	if (move_target.data != 24)
		return 2;
	return 0;
}
