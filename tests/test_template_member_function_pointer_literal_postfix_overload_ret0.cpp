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

char pick(Box);
long pick(int);

template <class T>
struct Holder {
	static int run() {
		using InnerResult = decltype((declval<Target>().*(&Target::make))());
		using PickResult = decltype(pick((declval<Target>().*(&Target::make))()));
		return sizeof(InnerResult) == sizeof(Box) &&
				sizeof(PickResult) == sizeof(char)
			? 0
			: 1;
	}
};

int main() {
	return Holder<int>::run();
}
