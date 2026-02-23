template<typename T>
struct Flags {
	T a : 3;
	T b : 5;
};

Flags<int> g;

int main() {
	g.a = 1;
	g.b = 3;
	return g.b - g.a;  // should be 2
}
