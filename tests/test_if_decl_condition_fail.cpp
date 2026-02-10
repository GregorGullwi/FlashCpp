// Test: if statement with declaration as condition
// Pattern: if (Type var = expr) - valid since C++98
// Currently expected to fail: parsing succeeds but codegen doesn't support
// variable declarations as if conditions yet (bad_any_cast).

typedef int myint;

myint compute() { return 42; }

int main() {
    myint result = 0;
    if (myint n = compute()) {
        result = n;
    }
    return result; // 42
}
