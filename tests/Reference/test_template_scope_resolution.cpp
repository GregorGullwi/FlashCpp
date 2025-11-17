// Test: Scope resolution with template
// Pattern: Template<T>::nested_type

template<typename T>
struct Traits {
    // This would be a typedef normally, but FlashCpp doesn't support typedef in templates
    // So we test with a nested struct instead
    struct nested {
        int value;
    };
};

// Can we parse Traits<int>::nested as a type?
int main() {
    Traits<int>::nested obj;
    obj.value = 42;
    return obj.value;
}
