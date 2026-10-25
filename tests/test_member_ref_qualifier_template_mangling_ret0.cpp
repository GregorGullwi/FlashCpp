// Regression: ref-qualified function-template specializations need distinct symbols.
template<class T> struct RefQualifiedTemplate {
	T value;
	template<class U> int select(U extra) & { return static_cast<int>(value) + static_cast<int>(extra) + 10; }
	template<class U> int select(U extra) && { return static_cast<int>(value) + static_cast<int>(extra) + 20; }
	template<class U> int operator()(U extra) & { return static_cast<int>(value) + static_cast<int>(extra) + 30; }
	template<class U> int operator()(U extra) && { return static_cast<int>(value) + static_cast<int>(extra) + 40; }
};
RefQualifiedTemplate<short>* identity(RefQualifiedTemplate<short>* pointer) { return pointer; }
int main() {
	RefQualifiedTemplate<short> value{2};
	if (identity(&value)->select(3) != 15) return 2;
	return value.select(3) == 15 && value.select<int>(3) == 15 &&
		RefQualifiedTemplate<short>{2}.select(3) == 25 &&
		RefQualifiedTemplate<short>{2}.select<int>(3) == 25 &&
		value(3) == 35 && RefQualifiedTemplate<short>{2}(3) == 45 ? 0 : 1;
}
