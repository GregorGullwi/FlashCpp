// A base-qualified direct assignment call selects the base's implicit
// operator=. The receiver type is Derived while the declaration belongs to
// Base, so ODR publication must use the declaration owner.
struct Base {
	int value;
};

struct Derived : Base {
	int tag;
};

int main() {
	Derived source;
	source.value = 42;
	source.tag = 7;
	Derived target;
	target.value = 0;
	target.tag = 0;
	target.Base::operator=(source);
	if (target.value != 42)
		return 1;

	Derived move_source;
	move_source.value = 24;
	move_source.tag = 9;
	Derived move_target;
	move_target.value = 0;
	move_target.tag = 0;
	move_target.Base::operator=(static_cast<Derived&&>(move_source));
	if (move_target.value != 24)
		return 2;
	return 0;
}
