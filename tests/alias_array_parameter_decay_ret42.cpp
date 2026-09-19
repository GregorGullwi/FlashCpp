using A = int[3];
template <class T, int N>
using B = T[N];
int first(A a) { return a[0] + a[2]; }
int second(B<short, 2> a) { return a[1]; }
int main() {
	A x = {18, 2, 20};
	B<short, 2> y = {1, 4};
	return first(x) + second(y) == 42 && sizeof(x) == 3 * sizeof(int) ? 42 : 0;
}
