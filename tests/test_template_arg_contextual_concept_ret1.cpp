template <class T>
concept HasMemberArrow = requires(T value) {
	value.operator->();
};

struct WithArrow {
	int operator->() const {
		return 1;
	}
};

struct WithoutArrow {};

template <class T>
int classifyArrowSupport() {
	if constexpr (HasMemberArrow<const T&>) {
		return 2;
	}
	return 1;
}

int main() {
	return (classifyArrowSupport<WithArrow>() == 2 &&
			classifyArrowSupport<WithoutArrow>() == 1)
		? 1
		: 0;
}
