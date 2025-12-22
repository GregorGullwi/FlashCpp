// Test: Return statement with braced initializer (C++20)
// Tests designated initializers in return statements

struct P
{
  int a;
};

P p()
{
 return { .a = 5 };
}

int main() {
    P result = p();
    return result.a;  // Should return 5
}
