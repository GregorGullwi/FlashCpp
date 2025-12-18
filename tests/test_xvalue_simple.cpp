// Simplified test for XValue support - rvalue reference cast
// This tests just the parser fix without complex template logic

int consume_rvalue(int&& x) {
    return x + 10;
}

int main() {
    int value = 5;
    
    // Test: Cast to rvalue reference (should be xvalue)
    int result = consume_rvalue(static_cast<int&&>(value));
    
    if (result != 15) return 1;  // 5 + 10 = 15
    
    return 0;  // Success
}
