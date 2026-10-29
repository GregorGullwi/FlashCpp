template <class T>
struct Box {
	T value;
	short tag;
};

template <>
struct Box<int> {
	int value;
	short tag;
};

template <class T>
struct DefaultedBox {
	T value;
	DefaultedBox& operator=(const DefaultedBox&) = default;
};

int main() {
	Box<short> source;
	source.value = 42;
	source.tag = 7;

	Box<short> copy_target;
	copy_target = source;
	if (copy_target.value != 42 || copy_target.tag != 7)
		return 1;

	Box<short> direct_target;
	direct_target.operator=(source);
	if (direct_target.value != 42 || direct_target.tag != 7)
		return 2;

	Box<short> move_target;
	move_target = static_cast<Box<short>&&>(source);
	if (move_target.value != 42 || move_target.tag != 7)
		return 3;

	Box<int> explicit_source;
	explicit_source.value = 64;
	explicit_source.tag = 8;
	Box<int> explicit_target;
	explicit_target = explicit_source;
	if (explicit_target.value != 64 || explicit_target.tag != 8)
		return 4;

	DefaultedBox<int> defaulted_source;
	defaulted_source.value = 11;
	DefaultedBox<int> defaulted_target;
	defaulted_target = defaulted_source;
	if (defaulted_target.value != 11)
		return 5;
	return 0;
}
