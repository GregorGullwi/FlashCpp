// Test parenthesized function-template declarations (MSVC macro-protection pattern)

// Declaration only (forward declaration with parenthesized name)
template <class T>
int (maxval)(T, T);

// Definition with parenthesized name
template <class T>
T (mymin)(T a, T b) {
return a < b ? a : b;
}

// Non-template version
int (add)(int a, int b) {
return a + b;
}

int main() {
int r = mymin(40, 42);   // 40
int s = add(1, 1);        // 2
return r + s - 42;        // 40 + 2 - 42 = 0
}
