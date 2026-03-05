// Test: specificity() correctly scores struct template instantiation patterns
// Verifies that a more specific specialization (with nested struct template pattern)
// beats the primary template when competing.

template<typename T, typename U>
struct Pair { T first; U second; };

// Primary (least specific)
template<typename T>
int v = 0;

// Specialization for any Pair<X,Y> - more specific than bare T
template<typename X, typename Y>
int v<Pair<X, Y>> = 1;

// Specialization for Pair<Pair<A,B>,Pair<C,D>> - most specific
template<typename A, typename B, typename C, typename D>
int v<Pair<Pair<A, B>, Pair<C, D>>> = 2;

int main() {
    // Bare type: should use primary (0)
    if (v<int> != 0) return 10;
    // Single Pair: should use Pair<X,Y> (1)
    if (v<Pair<int,float>> != 1) return 11;
    // Nested Pair: should use most specific (2)
    if (v<Pair<Pair<int,float>,Pair<double,char>>> != 2) return 12;
    return 0;
}
