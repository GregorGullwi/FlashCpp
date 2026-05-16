// Two-phase lookup for out-of-line static member initializers: multiple builtin arg types.
//
// select_overload(long) is the only overload visible at the template definition site.
// Post-definition overloads for int / short / unsigned are declared AFTER the template body,
// so they must NOT be selected (builtin types have no associated namespaces — ADL cannot
// find them at the point of instantiation per C++20 [temp.dep.candidate]).

int select_overload(long) { return 1; }

template<typename T>
struct Tagged {
	static int code;
};

template<typename T>
int Tagged<T>::code = select_overload(T{});

// Post-definition overloads — must NOT be selected for any of the instantiations below.
int select_overload(int) { return 2; }
int select_overload(short) { return 3; }
int select_overload(unsigned) { return 4; }

int main() {
	// All instantiations must pick the definition-site overload returning 1.
	if (Tagged<int>::code != 1) return 1;
	if (Tagged<long>::code != 1) return 2;
	if (Tagged<short>::code != 1) return 3;
	if (Tagged<unsigned int>::code != 1) return 4;
	return 0;
}
