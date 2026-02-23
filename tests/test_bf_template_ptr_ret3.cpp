template<typename T>
struct Flags {
	T a : 2;
	T b : 4;
};

int main() {
	Flags<int> f;
	f.a = 1;
	f.b = 4;
	Flags<int>* p = &f;
	return p->b - p->a;  // should be 3
}
