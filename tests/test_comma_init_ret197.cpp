// Test comma-separated declarations with different types
int main() {
    int a = 10, b = 20, c = 30;
    
    int x = 1, y = 2, z = 3;
    
    char ch1 = 'A', ch2 = 'B';
    
    return a + b + c + x + y + z + static_cast<int>(ch1) + static_cast<int>(ch2);  // 10+20+30+1+2+3+65+66 = 197
}
