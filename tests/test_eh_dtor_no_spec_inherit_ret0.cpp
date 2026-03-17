// Test: Per C++20 [class.dtor]/3, a destructor without an explicit noexcept
// specifier (including user-defined bodies and = default) inherits noexcept(false)
// from base/member destructors, just like an implicit destructor.

struct Base {
    int value;
    ~Base() noexcept(false) {}  // explicitly may throw
};

// User-defined body, no noexcept specifier — should inherit noexcept(false)
struct DerivedUserDefined : Base {
    ~DerivedUserDefined() {}
};

// Defaulted destructor — should inherit noexcept(false)
struct DerivedDefaulted : Base {
    ~DerivedDefaulted() = default;
};

// Member with noexcept(false) dtor, user-defined body, no specifier
struct HolderUserDefined {
    Base member;
    ~HolderUserDefined() {}
};

// Member with noexcept(false) dtor, defaulted
struct HolderDefaulted {
    Base member;
    ~HolderDefaulted() = default;
};

// Safe base: user-defined body, no specifier — inherits noexcept(true) (safe chain)
struct SafeBase {
    ~SafeBase() {}  // noexcept(true) by default
};
struct DerivedSafeUserDefined : SafeBase {
    ~DerivedSafeUserDefined() {}  // no specifier, should be noexcept(true)
};

int main() {
    constexpr int FAIL_DERIVED_USER_NOEXCEPT_OP   = 1;    // noexcept() returned wrong value for DerivedUserDefined
    constexpr int FAIL_DERIVED_USER_TRAIT          = 2;    // __is_nothrow_destructible wrong for DerivedUserDefined
    constexpr int FAIL_DERIVED_DEFAULT_NOEXCEPT_OP = 4;   // noexcept() returned wrong value for DerivedDefaulted
    constexpr int FAIL_DERIVED_DEFAULT_TRAIT       = 8;    // __is_nothrow_destructible wrong for DerivedDefaulted
    constexpr int FAIL_HOLDER_USER_NOEXCEPT_OP     = 16;  // noexcept() returned wrong value for HolderUserDefined
    constexpr int FAIL_HOLDER_USER_TRAIT           = 32;  // __is_nothrow_destructible wrong for HolderUserDefined
    constexpr int FAIL_HOLDER_DEFAULT_NOEXCEPT_OP  = 64;  // noexcept() returned wrong value for HolderDefaulted
    constexpr int FAIL_HOLDER_DEFAULT_TRAIT        = 128; // __is_nothrow_destructible wrong for HolderDefaulted
    constexpr int FAIL_SAFE_DERIVED_NOEXCEPT_OP    = 256; // noexcept() returned wrong value for DerivedSafeUserDefined
    constexpr int FAIL_SAFE_DERIVED_TRAIT          = 512; // __is_nothrow_destructible wrong for DerivedSafeUserDefined

    int result = 0;

    // DerivedUserDefined: user-defined body, no specifier, base has noexcept(false)
    if (noexcept(DerivedUserDefined{}.~DerivedUserDefined()))
        result |= FAIL_DERIVED_USER_NOEXCEPT_OP;
    if (__is_nothrow_destructible(DerivedUserDefined))
        result |= FAIL_DERIVED_USER_TRAIT;

    // DerivedDefaulted: = default, base has noexcept(false)
    if (noexcept(DerivedDefaulted{}.~DerivedDefaulted()))
        result |= FAIL_DERIVED_DEFAULT_NOEXCEPT_OP;
    if (__is_nothrow_destructible(DerivedDefaulted))
        result |= FAIL_DERIVED_DEFAULT_TRAIT;

    // HolderUserDefined: user-defined body, member has noexcept(false)
    if (noexcept(HolderUserDefined{}.~HolderUserDefined()))
        result |= FAIL_HOLDER_USER_NOEXCEPT_OP;
    if (__is_nothrow_destructible(HolderUserDefined))
        result |= FAIL_HOLDER_USER_TRAIT;

    // HolderDefaulted: = default, member has noexcept(false)
    if (noexcept(HolderDefaulted{}.~HolderDefaulted()))
        result |= FAIL_HOLDER_DEFAULT_NOEXCEPT_OP;
    if (__is_nothrow_destructible(HolderDefaulted))
        result |= FAIL_HOLDER_DEFAULT_TRAIT;

    // DerivedSafeUserDefined: user-defined body, no specifier, safe chain — should be noexcept
    if (!noexcept(DerivedSafeUserDefined{}.~DerivedSafeUserDefined()))
        result |= FAIL_SAFE_DERIVED_NOEXCEPT_OP;
    if (!__is_nothrow_destructible(DerivedSafeUserDefined))
        result |= FAIL_SAFE_DERIVED_TRAIT;

    return result;
}
