struct IntTag {
	char value;
};

struct LongTag {
	char value[2];
};

IntTag selectTag(int) {
	return {};
}

LongTag selectTag(long) {
	return {};
}

template<typename... Ts, typename Tail = int>
int classify(Ts..., Tail value = Tail{}) {
	return static_cast<int>(sizeof...(Ts)) * 10 + static_cast<int>(sizeof(selectTag(value)));
}

int main() {
	if (classify<char, short>(1, 2, 7L) != 22) {
		return 1;
	}

	if (classify<char, short>(1, 2) != 21) {
		return 2;
	}

	return 0;
}
