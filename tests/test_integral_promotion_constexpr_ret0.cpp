// C++20 [conv.prom]: constant evaluation applies integral promotion before ~.

constexpr unsigned char value = 200;
constexpr int complement = ~value;

int main() {
	return complement == -201 ? 0 : 1;
}
