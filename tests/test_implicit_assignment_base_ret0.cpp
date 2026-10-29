struct Base {
	int value;
};

struct Derived : Base {
	int tag;
};

template <class T>
struct TemplateBase {
	T value;
};

struct TemplateDerived : TemplateBase<short> {
	int tag;
};

struct VirtualDerived : virtual TemplateBase<short> {
	int tag;
};

struct UserBase {
	int value;
	UserBase& operator=(const UserBase& other) {
		value = other.value + 100;
		return *this;
	}
};

struct UserDerived : UserBase {
	int tag;
};

int main() {
	Derived source;
	source.value = 42;
	source.tag = 7;

	Derived copy_target;
	copy_target = source;
	if (copy_target.value != 42)
		return 1;
	if (copy_target.tag != 7)
		return 2;

	Derived move_target;
	move_target = static_cast<Derived&&>(source);
	if (move_target.value != 42)
		return 3;
	if (move_target.tag != 7)
		return 4;

	TemplateDerived template_source;
	template_source.value = 42;
	template_source.tag = 7;

	TemplateDerived template_copy_target;
	template_copy_target = template_source;
	if (template_copy_target.value != 42)
		return 5;
	if (template_copy_target.tag != 7)
		return 6;

	TemplateDerived template_move_target;
	template_move_target = static_cast<TemplateDerived&&>(template_source);
	if (template_move_target.value != 42)
		return 7;
	if (template_move_target.tag != 7)
		return 8;

	VirtualDerived virtual_source;
	virtual_source.value = 42;
	virtual_source.tag = 7;

	VirtualDerived virtual_target;
	virtual_target = virtual_source;
	if (virtual_target.value != 42)
		return 9;
	if (virtual_target.tag != 7)
		return 10;

	UserDerived user_source;
	user_source.value = 42;
	user_source.tag = 7;
	UserDerived user_target;
	user_target = user_source;
	if (user_target.value != 142)
		return 11;
	if (user_target.tag != 7)
		return 12;
	return 0;
}
