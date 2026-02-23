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

namespace ns {
    int get_value() { return 42; }
    
    namespace ns {
        int get_value() { return 10; }
    }
    
    int test() {
        // ::ns::get_value() must resolve to global ns (42), not ns::ns (10)
        return ::ns::get_value();
    }
}

int main() {
    if (ns::test() != 42) return 1;
    return outer::get_value();
}
