// Test constexpr constructor in template partial specialization
// This pattern is used in bits/enable_special_members.h
template<bool _Default, typename _Tag>
struct _Enable_default_constructor {
    constexpr _Enable_default_constructor() noexcept = default;
};

template<typename _Tag>
struct _Enable_default_constructor<false, _Tag> {
    constexpr _Enable_default_constructor() noexcept = delete;
    constexpr _Enable_default_constructor(_Enable_default_constructor const&) noexcept = default;
};

int main() {
    return 0;
}
