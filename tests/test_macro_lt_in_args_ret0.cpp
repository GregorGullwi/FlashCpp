// Test: less-than operator inside macro arguments
// The C preprocessor must not treat < as angle brackets when splitting macro args.
#define EMPTY2(a, b)
#define SELECT(a, b) (b)

EMPTY2(1 < 2, x)
EMPTY2(1 <= 2, x)
EMPTY2(1 << 2, x)

int main() {
    return SELECT(1 < 2, 0);
}
