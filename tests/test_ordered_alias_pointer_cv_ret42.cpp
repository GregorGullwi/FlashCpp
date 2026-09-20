using P = int*;
using CP = const P;
using PC = int* const;
int main() {
	const P (*(*a)[3])[4] = nullptr;
	CP (*(*b)[3])[4] = nullptr;
	PC (*(*c)[3])[4] = nullptr;
	return 42;
}
