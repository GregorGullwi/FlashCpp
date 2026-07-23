// Regression: pointer-to-member .* binds tighter than multiplicative *
// C++: a.*pm * 2 means (a.*pm)*2, not a.*(pm*2).

struct S {
	int v;
};

int main() {
	S a{3};
	int S::* pm = &S::v;
	return a.*pm * 2 - 6;
}
