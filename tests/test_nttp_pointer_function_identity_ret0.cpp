int ga = 5;
int gb = 7;

int fa() {
	return 3;
}

int fb() {
	return 4;
}

template <auto P>
struct Tag {
	static constexpr int value = -1;
};

template <>
struct Tag<&ga> {
	static constexpr int value = 10;
};

template <>
struct Tag<&gb> {
	static constexpr int value = 20;
};

template <>
struct Tag<&fa> {
	static constexpr int value = 30;
};

template <>
struct Tag<&fb> {
	static constexpr int value = 40;
};

int main() {
	if (Tag<&ga>::value != 10) {
		return 1;
	}
	if (Tag<&gb>::value != 20) {
		return 2;
	}
	if (Tag<&fa>::value != 30) {
		return 3;
	}
	if (Tag<&fb>::value != 40) {
		return 4;
	}

	return 0;
}
