int main() {
    int a = 1;
    int b = 0;
    int c = 1;
    
    int and_result = (a && c);    // 1 && 1 = 1
    int or_result = (b || c);     // 0 || 1 = 1
    int both = (a && c) || b;     // (1 && 1) || 0 = 1
    
    // Total: 1 + 1 + 1 = 3
    return and_result + or_result + both;
}
