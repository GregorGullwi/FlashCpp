// Test file for trailing requires clauses (C++20)
// Syntax: template<typename T> T func(T x) requires constraint

// Function template with trailing requires clause
template<typename T>
T double_value(T x) requires true {
    return x + x;
}

int main() {
    return 0;
}
