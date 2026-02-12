// Test that sizeof...() correctly matches pack names, not just any variadic parameter.
// When a member function template has its own variadic params AND sizeof... references
// the class template's pack, it should return the class pack size, not the function's.

template<typename... Elements>
struct PackHolder {
    static constexpr int class_pack_size() {
        return static_cast<int>(sizeof...(Elements));
    }
};

int main() {
    int size3 = PackHolder<int, float, double>::class_pack_size();
    if (size3 != 3) return 1;

    int size1 = PackHolder<int>::class_pack_size();
    if (size1 != 1) return 2;

    return 0;
}
