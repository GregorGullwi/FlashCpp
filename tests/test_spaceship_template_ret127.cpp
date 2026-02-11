// Test: spaceship operator with template struct
// C++20: defaulted operator<=> works with template instantiations
template<typename T>
struct Pair {
    T first;
    T second;
    auto operator<=>(const Pair&) const = default;
};

int main() {
    Pair<int> a{1, 2};
    Pair<int> b{1, 3};
    Pair<int> c{1, 2};
    
    int score = 0;
    if (a < b) score += 1;     // 1,2 < 1,3 -> true
    if (a == c) score += 2;    // 1,2 == 1,2 -> true
    if (a != b) score += 4;    // 1,2 != 1,3 -> true
    if (b > a) score += 8;     // 1,3 > 1,2 -> true
    if (a <= c) score += 16;   // 1,2 <= 1,2 -> true
    if (b >= a) score += 32;   // 1,3 >= 1,2 -> true
    if ((a <=> b) < 0) score += 64;  // inline expression
    
    return score;  // expect 127
}
