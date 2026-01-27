// Template with multiple type parameters
// Tests templates with 2+ type parameters
template<typename T, typename U>
T first(T a, U b) {
    return a;
}

template<typename T, typename U>
U second(T a, U b) {
    return b;
}

template<typename T, typename U>
T add_mixed(T a, U b) {
    return a + b;
}

int main() {
    // Explicit template arguments with multiple params
    int x = first<int, float>(42, 3.14f);
    float y = second<int, float>(42, 3.14f);
    
    // Deduced arguments with multiple params
    int z = first(10, 20);
    
    // Mixed types
    int w = add_mixed(5, 3);
    
    return x + y + z + w;
}

