struct Base {
	int value;
	Base(int v) : value(v) {}
	Base(const Base& other) : value(other.value + 1000) {}
};

struct VirtualConsumer {
	virtual int consume(Base value) {
		return value.value;
	}
};

struct Left : virtual public Base {
	Left(int v) : Base(v) {}
};

struct Right : virtual public Base {
	Right(int v) : Base(v) {}
};

struct Diamond : public Left, public Right {
	Diamond(int v) : Base(v), Left(v), Right(v) {}
};

struct Middle : virtual public Base {
	Middle(int v) : Base(v) {}
};

struct MostDerived : public Middle {
	MostDerived(int v) : Base(v), Middle(v) {}
};

Base* from_left(Left* value) {
	return value;
}

Base* from_right(Right* value) {
	return value;
}

Base* from_middle(Middle* value) {
	return value;
}

int take_base(Base value) {
	return value.value;
}

Base return_base(Diamond& value) {
	return value;
}

int main() {
	Diamond diamond(10);
	Base& reference = diamond;
	Base& explicit_reference = static_cast<Base&>(diamond);
	Base&& explicit_rvalue_reference = static_cast<Base&&>(diamond);
	Base* direct_pointer = &diamond;
	Base* explicit_pointer = static_cast<Base*>(&diamond);
	Left* left_pointer = &diamond;
	Right* right_pointer = &diamond;
	Left* null_left_pointer = nullptr;
	Base* null_base_pointer = null_left_pointer;

	MostDerived most_derived(20);
	Middle* middle_pointer = &most_derived;

	Base copied = diamond;
	Base returned = return_base(diamond);
	VirtualConsumer virtual_consumer;
	if (reference.value != 10)
		return 1;
	if (explicit_reference.value != 10)
		return 2;
	if (explicit_rvalue_reference.value != 10)
		return 3;
	if (direct_pointer->value != 10)
		return 4;
	if (explicit_pointer->value != 10)
		return 5;
	if (null_base_pointer != nullptr)
		return 6;
	if (from_left(left_pointer)->value != 10)
		return 7;
	if (from_right(right_pointer)->value != 10)
		return 8;
	if (from_middle(middle_pointer)->value != 20)
		return 9;
	if (copied.value != 1010)
		return 10;
	if (returned.value != 1010)
		return 11;
	if (take_base(diamond) != 1010)
		return 12;
	if (virtual_consumer.consume(diamond) != 1010)
		return 13;
	return 0;
}
