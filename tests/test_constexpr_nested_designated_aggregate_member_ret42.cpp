struct Inner {
	int val;
};

struct Outer {
	Inner inner;
	int extra;
};

constexpr Outer o = {.inner = {42}, .extra = 1};
constexpr int result = o.inner.val;

int main() {
	return result;
}
