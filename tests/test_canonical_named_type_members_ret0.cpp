// Published complete records publish named type-member schemas when every
// nested typedef/using RHS imports as Supported. Mixing native sizes proves
// schema publication does not abort compilation or change member layout.
struct Owner {
	using type = int;
	typedef double item;
	short tag;
};

int useOwner(Owner value) {
	Owner::type count = value.tag;
	Owner::item scale = 1.5;
	return static_cast<int>(count + scale);
}

int main() {
	Owner value{};
	value.tag = 3;
	return useOwner(value) - 4;
}
