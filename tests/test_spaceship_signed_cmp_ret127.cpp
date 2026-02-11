// Test spaceship operator with signed integer members requiring correct signed comparison
struct Pair {
    int a;
    int b;
    auto operator<=>(const Pair&) const = default;
};

int main() {
    Pair x{-10, 5};
    Pair y{10, 5};
    
    int score = 0;
    // Signed comparison: -10 < 10
    if (x < y) score += 1;
    if (y > x) score += 2;
    if (x != y) score += 4;
    
    // Same first, differ in second (signed -3 < 3)
    Pair a{5, -3};
    Pair b{5, 3};
    if (a < b) score += 8;
    
    // Equality with negative values
    Pair c{-5, -5};
    Pair d{-5, -5};
    if (c == d) score += 16;
    if (c <= d) score += 32;
    if (c >= d) score += 64;
    
    return score; // expect 127
}
