struct ClassScopeDecltypeNonstaticBase {
	int inherited() {
		return 1;
	}
};

template <class T>
struct ClassScopeDecltypeNonstaticDerived : ClassScopeDecltypeNonstaticBase {
	using inherited_result = decltype(inherited());
};

int main() {
	return 0;
}
