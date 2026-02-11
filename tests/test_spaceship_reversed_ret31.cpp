// Test spaceship operator with reversed operand order and self-comparison
struct Val {
    int x;
    auto operator<=>(const Val&) const = default;
};

int main() {
    Val a{5};
    Val b{10};
    
    int score = 0;
    // Direct <=> call
    int cmp1 = a <=> b;  // 5 <=> 10 = -1
    if (cmp1 < 0) score += 1;
    
    int cmp2 = b <=> a;  // 10 <=> 5 = 1
    if (cmp2 > 0) score += 2;
    
    // Inline usage with reversed operands
    if ((a <=> b) < 0) score += 4;
    if ((b <=> a) > 0) score += 8;
    
    // Self-comparison
    if ((a <=> a) == 0) score += 16;
    
    return score;  // expect 31
}
