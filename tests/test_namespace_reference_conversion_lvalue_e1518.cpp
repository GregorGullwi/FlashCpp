// Binding a namespace-scope reference to a class lvalue through a conversion
// function needs the conversion result's address, not the class object's
// address. The static relocation path would bind the wrong subobject, so the
// runtime form is diagnosed.

struct MixedHolder {
	long long wide;
	int narrow;
	char tag;

	operator int&() {
		return narrow;
	}
};

MixedHolder holder{4000000000LL, 17, 3};
int& r = holder;

int main() {
	r = 7;
	return holder.narrow == 7 ? 0 : 1;
}
