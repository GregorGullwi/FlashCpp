// Test full template specialization (template<>)

template<typename T>
struct Container {
    T value;
    int type_id;
};

// Full specialization for bool
template<>
struct Container<bool> {
    unsigned char bits;  // Different layout
    int type_id;
};

int main() {
    // Use primary template
    Container<int> int_container;
    int_container.value = 42;
    int_container.type_id = 1;
    
    // Use full specialization
    Container<bool> bool_container;
    bool_container.bits = 1;
    bool_container.type_id = 2;
    
    // Return sum to verify both work
    return int_container.value + bool_container.bits + int_container.type_id + bool_container.type_id - 46;  // Should be 0
}
