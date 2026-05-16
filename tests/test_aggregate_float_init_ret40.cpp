// Aggregate initialization with floating-point members
// Expected return: 40
struct S {
	int x;
	float y;
};
int main() {
	S s = S{2, 40.0f};
	return static_cast<int>(s.y);
}
