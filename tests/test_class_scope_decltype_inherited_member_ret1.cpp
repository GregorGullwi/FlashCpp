struct ClassScopeDecltypeBase {
	static int inherited() {
		return 1;
	}
};

template <class T>
struct ClassScopeDecltypeDerived : ClassScopeDecltypeBase {
	using inherited_result = decltype(inherited());

	inherited_result call() {
		return inherited();
	}
};

int main() {
	ClassScopeDecltypeDerived<int> value;
	return value.call();
}
