// Test template with braced initializers - simpler version
template<typename T>
struct Simple {
   T x = {10};
   T y = {20};
};

int main() {
   Simple<int> s;
   int a = s.x;
   int b = s.y;
   return a + b;  // Expected: 30
}
