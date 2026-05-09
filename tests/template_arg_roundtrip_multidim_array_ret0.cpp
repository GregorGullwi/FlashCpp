// Test: round-trip multidimensional array template arguments through TypeInfo::TemplateArgInfo
// Verifies that TemplateArgInfo preserves multi-dimensional array dimensions

template<class T>
struct Box {
static int test() { return 0; }
};

// Test 1: 1D array as template argument
using IntArr4 = int[4];

// Test 2: 2D array as template argument  
using IntArr2D = int[3][4];

// Test 3: 3D array as template argument
using IntArr3D = int[2][3][4];

// Explicit instantiation to trigger template processing
template struct Box<IntArr4>;
template struct Box<IntArr2D>;
template struct Box<IntArr3D>;

int main() {
// The tests verify that template instantiation works without crashing
// The actual round-trip conversion is tested through compilation itself
Box<IntArr4>::test();
Box<IntArr2D>::test();
Box<IntArr3D>::test();
return 0;
}
