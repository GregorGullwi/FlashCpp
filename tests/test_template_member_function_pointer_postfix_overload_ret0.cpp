template <typename T>
T declval();

struct Box {
	int value;
};

struct Target {
	Box make() {
		return Box{42};
	}
};

template <class T>
struct is_same {
	static constexpr bool value = false;
};

template <>
struct is_same<Box> {
	static constexpr bool value = true;
};

char pick(Box);
long pick(int);

template <class T>
struct Holder {
	static int run() {
		using InnerResult =
			decltype((declval<Target>().*declval<Box (Target::*)()>())());
		using PickResult =
			decltype(pick((declval<Target>().*declval<Box (Target::*)()>())()));
		if (!is_same<InnerResult>::value) {
			return 1;
		}
		return sizeof(PickResult) == sizeof(char) ? 0 : 2;
	}
};

int main() {
	return Holder<int>::run();
}
