// Test: Does the template::nested syntax work for RETURN TYPES?

template<typename T>
struct Wrapper {
    struct Inner {
        int value;
    };
};

// Function returning nested type from template
Wrapper<int>::Inner get_inner() {
    Wrapper<int>::Inner result;
    result.value = 99;
    return result;
}

int main() {
    return 0;
}
