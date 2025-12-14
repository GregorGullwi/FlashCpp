// Test template with empty braced initializers
template<typename T>
struct Simple {
   T x = {};
   T y = {};
};

int main() {
   Simple<int> s;
   return s.x + s.y;  // Expected: 0 (default initialized)
}
