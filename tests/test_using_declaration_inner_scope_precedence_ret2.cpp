// Inner block-scope using-declarations must shadow enclosing using-declarations
// for the same local name (C++20 [namespace.udecl] lookup in the inner scope).

namespace A {
int value = 1;
}

namespace B {
int value = 2;
}

int main() {
	using A::value;
	{
		using B::value;
		return value;
	}
}
