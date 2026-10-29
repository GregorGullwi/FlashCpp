struct Counted {
	int value;
	Counted& operator=(const Counted& other) {
		value = other.value + 100;
		return *this;
	}
};

struct Inner {
	Counted counted;
};

struct Holder {
	int tag;
	Inner inner;
	Counted direct;
	Counted counted[2];
};

template <class T>
struct Box {
	T value;
	Counted counted;
};

int main() {
	Holder source;
	source.tag = 7;
	source.inner.counted.value = 3;
	source.direct.value = 4;
	source.counted[0].value = 5;
	source.counted[1].value = 6;

	Holder copy_target;
	copy_target = source;
	if (copy_target.tag != 7)
		return 1;
	if (copy_target.inner.counted.value != 103)
		return 2;
	if (copy_target.direct.value != 104)
		return 3;
	if (copy_target.counted[0].value != 105 || copy_target.counted[1].value != 106)
		return 4;

	Holder move_target;
	move_target = static_cast<Holder&&>(source);
	if (move_target.inner.counted.value != 103)
		return 5;
	if (move_target.direct.value != 104)
		return 6;
	if (move_target.counted[0].value != 105 || move_target.counted[1].value != 106)
		return 7;

	Box<int> template_source;
	template_source.value = 9;
	template_source.counted.value = 5;
	Box<int> template_target;
	template_target = template_source;
	if (template_target.value != 9)
		return 8;
	if (template_target.counted.value != 105)
		return 9;
	return 0;
}
