enum class Color { Red };
struct S {
	int x;
	S(int v) : x(v) {}
};
int main() {
	S s{Color::Red};	 // should fail: cannot implicitly convert scoped enum
	return s.x;
}
