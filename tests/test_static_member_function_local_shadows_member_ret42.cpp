// A local variable in a static member function shadows a same-named non-static
// data member. The local lookup must win, so the implicit-this rejection must
// not fire merely because the name also matches a non-static data member.

struct StaticLocalShadow {
	int value;
	static int get() {
		int value = 42;
		return value;
	}
};

int main() {
	return StaticLocalShadow::get();
}
