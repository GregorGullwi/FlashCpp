// Selected member partial signatures must substitute enclosing and local parameters.
template<class T> struct SignatureOwner {
	template<class U> struct Box { U value; };
	template<class U> struct Box<U*> {
		T stored;
		Box() : stored(0) {}
		explicit Box(T value) : stored(value) {}
		T declared(T value);
		T& reference(T& value) { return value; }
		const T* pointer(const T* value) const { return value; }
		T callback(T (*function)(T), T value) { return function(value); }
		T echo(T value) { return value; }
		T mix(U value, T extra) { return static_cast<T>(value) + extra; }
	};
};
short narrowCallback(short value) { return value + 2; }
long long wideCallback(long long value) { return value + 3; }
int main() {
	SignatureOwner<short>::Box<int*> narrow{};
	SignatureOwner<long long>::Box<int*> wide{};
	if (narrow.echo(7) != 7 || wide.echo(5000000000LL) != 5000000000LL) return 1;
	if (narrow.mix(4, 3) != 7 || wide.mix(6, 5000000000LL) != 5000000006LL) return 2;
	SignatureOwner<short>::Box<int*> constructed_narrow(13);
	SignatureOwner<long long>::Box<int*> constructed_wide(5000000007LL);
	if (constructed_narrow.stored != 13 || constructed_wide.stored != 5000000007LL) return 6;
	short left = 8;
	long long right = 5000000000LL;
	narrow.reference(left) = 9;
	wide.reference(right) = 5000000001LL;
	if (left != 9 || right != 5000000001LL) return 3;
	if (*narrow.pointer(&left) != 9 || *wide.pointer(&right) != 5000000001LL) return 4;
	if (narrow.callback(narrowCallback, 4) != 6 || wide.callback(wideCallback, 5000000000LL) != 5000000003LL) return 5;
	return 0;
}
