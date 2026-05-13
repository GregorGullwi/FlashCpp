struct S {
	int x;
	S(int v) : x(v) {}
};

using A1 = S;
using A2 = A1;
using A3 = A2;

int main() {
	return A3(42).x;
}
