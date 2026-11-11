struct Left {
	int value;
};

struct Right {
	int value;
};

struct Derived : Left, Right {
	using Left::value;
};

int main() {
	Derived value{};
	value.value = 7;
	return value.value == 7 ? 0 : 1;
}
