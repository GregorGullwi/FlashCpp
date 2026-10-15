template <class T>
using Fn = int (T);

template <class T>
using FnPtr = int (*)(T);

int triple(int x) { return 3 * x; }
int twice(double x) { return static_cast<int>(2 * x); }

int apply(Fn<int>* f, int x) { return f(x); }

int main() {
	Fn<int>* int_fn = &triple;
	Fn<double>* double_fn = &twice;
	if (int_fn(4) != 12) return 1;
	if (double_fn(8.0) != 16) return 2;
	if (apply(&triple, 5) != 15) return 3;
	if (apply(int_fn, 6) != 18) return 4;
	FnPtr<int> int_fn_ptr = &triple;
	FnPtr<double> double_fn_ptr = &twice;
	if (int_fn_ptr(7) != 21) return 5;
	if (double_fn_ptr(4.0) != 8) return 6;
	return 0;
}
