struct Left {
	static int value;
};

struct Right {
	static int value;
};

struct Derived : Left, Right {
	using Left::value;
};

int Left::value = 3;
int Right::value = 4;

int main() {
	return Derived::value == 3 ? 0 : 1;
}
