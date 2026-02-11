// Test spaceship operator with long long member types
struct BigVal {
    long long a;
    long long b;
    auto operator<=>(const BigVal&) const = default;
};

int main() {
    BigVal x{1000000000LL, 2000000000LL};
    BigVal y{1000000000LL, 2000000001LL};
    BigVal z{1000000000LL, 2000000000LL};
    
    int score = 0;
    if (x < y) score += 1;
    if (x == z) score += 2;
    if (x != y) score += 4;
    if (y > x) score += 8;
    
    return score;  // expect 15
}
