struct Key { int value; };
constexpr Key key{1};
template<Key Value> struct Base { short member; };
struct Padding { long long guard; };
struct Derived : Padding, Base<key> {};
int main() {
	Derived object;
	object.guard = 12345678901LL;
	object.member = 42;
	Base<key>* base = &object;
	return base->member == 42 && object.guard == 12345678901LL ? 0 : 1;
}
