// A member partial-specialization method body may use a parameter from the
// enclosing class template. Substitution must combine the effective owner
// bindings with the partial's deduced local bindings.
template<class T> struct Owner {
	template<class U> struct Box { U value; };
	template<class U> struct Box<U*> {
		int add(U right) {
			T left = 3;
			return static_cast<int>(left) + right;
		}
	};
};

int main() {
	Owner<short>::Box<int*> narrow{};
	Owner<long long>::Box<int*> wide{};
	const int narrow_total = narrow.add(4);
	const int wide_total = wide.add(6);
	return narrow_total + wide_total - 16;
}
