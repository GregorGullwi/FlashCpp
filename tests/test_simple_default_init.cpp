// Test non-template struct with default member initializers
struct Simple {
   int x = 10;
   int y = 20;
};

int main() {
   Simple s;
   return s.x + s.y;  // Expected: 30
}
