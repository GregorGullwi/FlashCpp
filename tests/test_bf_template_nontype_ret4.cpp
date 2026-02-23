template<typename T, int W1, int W2>
struct Packed {
	T a : W1;
	T b : W2;
};

int main() {
	Packed<int, 3, 4> p;
	p.a = 3;
	p.b = 7;
	return p.b - p.a;  // should be 4
}
