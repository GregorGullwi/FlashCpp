// Test: deeply nested variable template partial specialization deduction
// Verifies that v<Pair<Pair<A,B>,Pair<C,D>>> correctly deduces all 4 type params
// when the same template (Pair) appears in multiple nested positions.

template <typename T, typename U>
struct Pair {
	T first;
	U second;
};

// Primary variable template (default: -1)
template <typename T>
int v = -1;

// Partial specialization: same template (Pair) appears in two nested positions
// All 4 independent type params should be deduced correctly
template <typename A, typename B, typename C, typename D>
int v<Pair<Pair<A, B>, Pair<C, D>>> = sizeof(A) + sizeof(B) + sizeof(C) + sizeof(D);

int main() {
 // A=int(4), B=float(4), C=double(8), D=char(1) -> 4+4+8+1=17
	return v<Pair<Pair<int, float>, Pair<double, char>>> == 17 ? 0 : 1;
}
