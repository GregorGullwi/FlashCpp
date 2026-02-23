// Test: sibling namespace qualified name resolution
// When inside namespace outer, inner::type should resolve to outer::inner::type
namespace outer {
namespace inner {
    using type = signed char;
    enum class _Ord : type { equivalent = 0, less = -1, greater = 1 };
}

class ordering {
    inner::type _M_value;
public:
    constexpr ordering(inner::type v) : _M_value(v) {}
    constexpr inner::type value() const { return _M_value; }
};

int get_value() {
    ordering o{42};
    return o.value();
}
}

int main() {
    return outer::get_value();
}
