// Test: extern template with namespace-qualified class names
// Bug: Parser failed on "extern template class ns::Name<T>;" because it only consumed
// one token as the class name, not handling '::' for namespace-qualified names.

namespace ns {
    template<typename T>
    struct Container {
        T value;
    };
}

// This should parse without errors (extern suppresses instantiation, so we don't call members)
extern template class ns::Container<int>;

int main() {
    return 0;
}
