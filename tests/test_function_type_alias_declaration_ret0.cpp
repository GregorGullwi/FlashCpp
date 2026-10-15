using PlainFn = int (int);
PlainFn declared;
int declared(int x) { return x + 1; }

template <class T>
using Fn = int (T);
Fn<int> aliased;
int aliased(int x) { return x * 2; }

using TwoArgFn = long (int, long);
TwoArgFn combine;
long combine(int a, long b) { return a + b; }

using VoidFn = void (int);
VoidFn forward_declared;
void forward_declared(int x);

int observed = 0;
void forward_declared(int x) { observed = x; }

int main() {
	if (declared(6) != 7) return 1;
	if (aliased(5) != 10) return 2;
	if (combine(2, 3L) != 5L) return 3;
	forward_declared(9);
	if (observed != 9) return 4;
	return 0;
}
