// Test non-template struct with empty braced initializers
struct Simple {
   int x = {};
   int y = {};
};

int main() {
   Simple s;
   return s.x + s.y;  // Expected: 0 (default initialized)
}
