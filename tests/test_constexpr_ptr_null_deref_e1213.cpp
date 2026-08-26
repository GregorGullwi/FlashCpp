// Dereferencing nullptr in a constant expression is undefined behavior and must
// be rejected as not a constant expression.

constexpr int readValue(const int* p) {
	return *p;
}

constexpr int invalid = readValue(nullptr);

int main() {
	return 0;
}
