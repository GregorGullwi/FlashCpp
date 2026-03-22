// Test: ternary operator returning struct types in constexpr context

struct Pt { int x; int y; };

// Global constexpr variable initialized from ternary - true branch
constexpr Pt p1 = (true ? Pt{3, 7} : Pt{1, 2});
static_assert(p1.x == 3);
static_assert(p1.y == 7);

// Global constexpr variable initialized from ternary - false branch
constexpr Pt p2 = (false ? Pt{3, 7} : Pt{1, 2});
static_assert(p2.x == 1);
static_assert(p2.y == 2);

// Constexpr function returning struct via ternary
constexpr Pt choose(bool b) {
    return b ? Pt{10, 20} : Pt{30, 40};
}
constexpr Pt r1 = choose(true);
static_assert(r1.x == 10);
static_assert(r1.y == 20);

constexpr Pt r2 = choose(false);
static_assert(r2.x == 30);
static_assert(r2.y == 40);

int main() { return 0; }
