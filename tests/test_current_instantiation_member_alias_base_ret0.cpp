// Regression: member alias templates that rebound to the current instantiation
// should reuse the active specialization when they appear in a nested base clause,
// instead of recursively re-instantiating the owning class template.

template<typename T>
struct Box {
    template<typename U>
    using rebound = Box<U>;

    struct Inner : rebound<T> {
        int value = 42;
    };
};

int main() {
    Box<int>::Inner inner;
    return inner.value == 42 ? 0 : 1;
}
