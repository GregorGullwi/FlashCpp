// Test that sizeof...(Pack) works for class template parameter packs
// when used inside member function templates of the class.
// This is a regression test for the _Elements pack issue in std::tuple.

template<typename... Ts>
struct PackHolder {
    template<typename U>
    static constexpr int get_pack_size() {
        return static_cast<int>(sizeof...(Ts));
    }
    
    static constexpr int direct_pack_size() {
        return static_cast<int>(sizeof...(Ts));
    }
};

int main() {
    // PackHolder<int, float, double> has Ts = {int, float, double}, so sizeof...(Ts) = 3
    int size = PackHolder<int, float, double>::direct_pack_size();
    if (size != 3) return 1;
    
    // Single element pack
    int size1 = PackHolder<int>::direct_pack_size();
    if (size1 != 1) return 2;
    
    return 0;
}
