// Test: Global-scope-qualified types in type specifier
// Verifies parsing of ::Namespace::Type pattern used in headers like <vector>
namespace outer {
    int compute() { return 42; }
}

int main() {
    return outer::compute();
}
