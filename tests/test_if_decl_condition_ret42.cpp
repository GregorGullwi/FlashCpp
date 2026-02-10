// Test: if statement with declaration as condition
// Pattern: if (Type var = expr) - valid since C++98

int compute() { return 42; }

int main() {
    int result = 0;
    if (int n = compute()) {
        result = n;
    }
    return result; // 42
}
