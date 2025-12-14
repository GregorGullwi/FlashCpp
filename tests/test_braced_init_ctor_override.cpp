// Test template with constructor that overrides default member initializers
template<typename T>
struct Simple {
   T x = {10};
   T y = {20};
   
   Simple() : x(100), y(200) {}  // Constructor overrides defaults
};

int main() {
   Simple<int> s;
   return s.x + s.y;  // Expected: 300 (constructor values)
}
