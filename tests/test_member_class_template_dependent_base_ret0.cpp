struct Payload {
	int value;
	char tag;
	long long wide_value;
};

template <class Owner>
struct Container {
	template <class Base>
	struct Nested : Base {
		int own_value;

		struct Inner : Base {
			int inner_value;
		};
	};
};

int main() {
	Container<int>::Nested<Payload> value{};
	value.value = 30;
	value.tag = 1;
	value.wide_value = 7;
	value.own_value = 2;
	Container<int>::Nested<Payload>::Inner inner{};
	inner.value = 20;
	inner.tag = 1;
	inner.wide_value = 17;
	inner.inner_value = 4;
	return value.value + value.tag + value.wide_value + value.own_value == 40 &&
		inner.value + inner.tag + inner.wide_value + inner.inner_value == 42 ? 0 : 1;
}
