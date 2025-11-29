// Test multiple simple alias templates

template<typename T>
using Ptr = T*;

template<typename T>
using Ptr2 = T*;

int main() {
    int x = 10;
    int y = 20;
    
    Ptr<int> p1 = &x;
    Ptr2<int> p2 = &y;
    
    return *p1 + *p2;  // 10 + 20 = 30
}
