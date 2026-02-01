// Test out-of-line template member with multiline return type and qualifiers.

template<typename T>
struct Wrapper {
    struct Nested {
        T value;
    };

    const typename Wrapper<T>::Nested*
    get() const;

    Wrapper(T v) : nested{v} {}

    Nested nested;
};

template<typename T>
const typename Wrapper<T>::Nested*
Wrapper<T>::get() const {
    return &nested;
}

int main() {
    Wrapper<int> wrapper(42);
    return wrapper.get()->value;
}
