int ga = 1;
int gb = 2;

template <int* P>
struct Tag {
	static int value() {
		return -1;
	}
};

template <>
struct Tag<&ga> {
	static int value() {
		return 10;
	}
};

template <>
struct Tag<&gb> {
	static int value() {
		return 20;
	}
};

int main() {
	return Tag<&ga>::value() == 10 && Tag<&gb>::value() == 20 ? 0 : 1;
}
