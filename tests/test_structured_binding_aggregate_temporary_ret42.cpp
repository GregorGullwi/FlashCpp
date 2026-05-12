// Structured binding should work for temporary aggregate objects as well.
// Expected return: 42

struct S {
int x;
int y;
};

int main() {
auto [a, b] = S{2, 40};
return a + b;
}
