template<typename T>
struct Flags {
	T a : 2;
	T b : 4;
};

int main() {
	Flags<int> f;
	f.a = 1;
	f.b = 4;
	return f.b - f.a;  // should be 3
}
