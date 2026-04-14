// Tests that template constructor bodies can access struct members via
// implicit 'this' and that multiple members are correctly stored.
int gResult = 0;

struct MultiMember {
	int a;
	int b;
	int c;

	template<typename T>
	MultiMember(T x, T y) {
		a = (int)x;
		b = (int)y;
		c = a + b;
	}

	~MultiMember() {
		gResult = a + b + c;
	}
};

int main() {
	{
		MultiMember m(10, 11);
	}
	// a=10, b=11, c=21, gResult = 10+11+21 = 42
	return gResult;
}
