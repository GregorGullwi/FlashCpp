// A direct operator= call on a nested class must publish the implicit
// assignment ODR use through the receiver's canonical owner TypeId rather than
// relying on the declaration's parent spelling.
struct Outer {
	struct Inner {
		int value;
	};
};

int main() {
	Outer::Inner source;
	source.value = 42;
	Outer::Inner target;
	target.value = 0;
	target.operator=(source);
	if (target.value != 42)
		return 1;

	Outer::Inner move_source;
	move_source.value = 24;
	Outer::Inner move_target;
	move_target.value = 0;
	move_target.operator=(static_cast<Outer::Inner&&>(move_source));
	if (move_target.value != 24)
		return 2;
	return 0;
}
