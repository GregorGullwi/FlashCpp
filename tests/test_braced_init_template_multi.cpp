// Test multiple template instantiations with braced initializers
template<typename T>
struct Simple {
   T x = {10};
   T y = {20};
};

int main() {
   Simple<int> s1;
   Simple<long long> s2;
   Simple<short> s3;
   return (s1.x + s1.y) + (s2.x + s2.y) + (s3.x + s3.y);  // Expected: 30 + 30 + 30 = 90
}
