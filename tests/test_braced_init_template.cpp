// Simpler template test
template<typename T>
struct Simple {
   T x = {10};
   T y = {20};
};

int main() {
   Simple<int> s;
   return s.x + s.y;  // Expected: 30
}
