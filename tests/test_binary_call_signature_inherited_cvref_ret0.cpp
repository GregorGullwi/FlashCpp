namespace ops {
	struct Padding { long long guard; };
	struct Base {
		short operator+(int& right) const & { return static_cast<short>(right + 3); }
		long long operator+(long long&& right) const && { return right + 5; }
	};
	struct Derived : Padding, Base {};

	template<class T> struct TemplateBase {
		T operator+(int& right) const & { return static_cast<T>(right + 7); }
	};
	template<class T> struct TemplateDerived : Padding, TemplateBase<T> {};
}

int main() {
	const ops::Derived derived{};
	ops::Derived movable{};
	int value = 10;
	long long wide = 10;
	ops::TemplateDerived<long long> generic{};
	static_assert(sizeof(decltype(derived + value)) == sizeof(short));
	static_assert(sizeof(decltype(static_cast<ops::Derived&&>(movable) + 10LL)) == sizeof(long long));
	static_assert(sizeof(decltype(generic + value)) == sizeof(long long));
	if (derived + value != 13) return 1;
	if (static_cast<ops::Derived&&>(movable) + static_cast<long long&&>(wide) != 15) return 2;
	if (generic + value != 17) return 3;
	return 0;
}
