// Comprehensive alias template test

// Simple pointer alias
template<typename T>
using Ptr = T*;

// Const pointer alias
template<typename T>
using ConstPtr = const T*;

// Reference alias
template<typename T>
using Ref = T&;

// Double pointer
template<typename T>
using PtrPtr = T**;

int main() {
    int x = 10;
    int y = 20;
    int z = 30;
    
    // Test Ptr<int> = int*
    Ptr<int> p1 = &x;
    int result1 = *p1;  // 10
    
    // Test ConstPtr<int> = const int*
    ConstPtr<int> p2 = &y;
    int result2 = *p2;  // 20
    
    // Test Ref<int> = int&
    Ref<int> r = z;
    int result3 = r;  // 30
    
    // Test PtrPtr<int> = int**
    int* temp = &x;
    PtrPtr<int> pp = &temp;
    int result4 = **pp;  // 10
    
    // Total: 10 + 20 + 30 + 10 = 70
    return result1 + result2 + result3 + result4;
}
