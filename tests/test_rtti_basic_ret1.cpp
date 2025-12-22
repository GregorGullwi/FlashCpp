// Basic RTTI tests for typeid and dynamic_cast

// Test 1: Simple polymorphic hierarchy
struct Base {
    int base_value;
    
    Base() : base_value(10) {}
    
    virtual int getValue() {
        return base_value;
    }
    
    virtual ~Base() {}
};

struct Derived : public Base {
    int derived_value;
    
    Derived() : derived_value(20) {
        base_value = 15;
    }
    
    virtual int getValue() {
        return base_value + derived_value;
    }
};

// Test 2: typeid with polymorphic type (compile-time)
int test_typeid_type() {
    // For now, just test that typeid compiles
    // In a full implementation, we'd compare type_info objects
    const void* ti = typeid(Base);
    const void* ti2 = typeid(Derived);
    
    // Return 1 if both are non-null (basic sanity check)
    if (ti && ti2) {
        return 1;
    }
    return 0;
}

// Test 3: typeid with expression
int test_typeid_expr() {
    Derived d;
    d.base_value = 5;
    
    // Get type info from expression
    const void* ti = typeid(d);
    
    // Return 1 if non-null
    if (ti) {
        return 1;
    }
    return 0;
}

// Test 4: dynamic_cast successful downcast
int test_dynamic_cast_success() {
    Derived d;
    d.base_value = 7;
    d.derived_value = 13;
    
    Base* base_ptr = &d;
    Derived* derived_ptr = dynamic_cast<Derived*>(base_ptr);
    
    if (derived_ptr) {
        return derived_ptr->base_value + derived_ptr->derived_value;  // Expected: 20
    }
    return 0;
}

// Test 5: dynamic_cast failed downcast
int test_dynamic_cast_fail() {
    Base b;
    b.base_value = 100;
    
    Base* base_ptr = &b;
    Derived* derived_ptr = dynamic_cast<Derived*>(base_ptr);
    
    // Should return nullptr for failed cast
    if (derived_ptr) {
        return 0;  // Failed - should have been null
    }
    return 1;  // Success - correctly returned null
}

// Test 6: dynamic_cast with virtual function call
int test_dynamic_cast_virtual() {
    Derived d;
    d.base_value = 3;
    d.derived_value = 4;
    
    Base* base_ptr = &d;
    Derived* derived_ptr = dynamic_cast<Derived*>(base_ptr);
    
    if (derived_ptr) {
        return derived_ptr->getValue();  // Expected: 7 (3 + 4)
    }
    return 0;
}

// Test 7: Multiple levels of inheritance
struct MoreDerived : public Derived {
    int more_value;
    
    MoreDerived() : more_value(30) {
        base_value = 1;
        derived_value = 2;
    }
    
    virtual int getValue() {
        return base_value + derived_value + more_value;
    }
};

int test_dynamic_cast_multilevel() {
    MoreDerived md;
    md.base_value = 5;
    md.derived_value = 10;
    md.more_value = 15;
    
    Base* base_ptr = &md;
    MoreDerived* more_ptr = dynamic_cast<MoreDerived*>(base_ptr);
    
    if (more_ptr) {
        return more_ptr->getValue();  // Expected: 30 (5 + 10 + 15)
    }
    return 0;
}

// Test 8: Cross-cast (sibling cast should fail)
struct OtherDerived : public Base {
    int other_value;
    
    OtherDerived() : other_value(50) {
        base_value = 25;
    }
    
    virtual int getValue() {
        return base_value + other_value;
    }
};

int test_dynamic_cast_cross() {
    Derived d;
    d.base_value = 10;
    
    Base* base_ptr = &d;
    OtherDerived* other_ptr = dynamic_cast<OtherDerived*>(base_ptr);
    
    // Should return nullptr for cross-cast
    if (other_ptr) {
        return 0;  // Failed - should have been null
    }
    return 1;  // Success - correctly returned null
}


int main() {
    return test_typeid_type();
}
