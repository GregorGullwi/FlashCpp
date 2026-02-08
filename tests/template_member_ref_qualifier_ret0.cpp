// Test: ref-qualifiers on template member functions inside template structs
template<typename _Tp>
struct optional {
    _Tp value;

    template<typename _Up>
    _Tp value_or(_Up&& __u) const& {
        return value;
    }

    template<typename _Up>
    _Tp value_or2(_Up&& __u) && {
        return _Tp();
    }
};

int main() {
    return 0;
}
