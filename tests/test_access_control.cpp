// Access control tests for inheritance in FlashCpp

// Test 1: Public inheritance - public members accessible
struct PublicBase {
public:
    int public_member;
    
    PublicBase() : public_member(10) {}
    
    int getPublic() {
        return public_member;
    }
};

struct PublicDerived : public PublicBase {
    int derived_member;
    
    PublicDerived() : derived_member(20) {}
    
    int accessBasePublic() {
        return public_member;  // Should be accessible
    }
    
    int callBaseMethod() {
        return getPublic();  // Should be accessible
    }
};

int test_public_inheritance() {
    PublicDerived d;
    return d.public_member + d.accessBasePublic() + d.callBaseMethod();  // Expected: 30 (10 + 10 + 10)
}

// Test 2: Protected members - accessible in derived class
struct ProtectedBase {
protected:
    int protected_member;
    
public:
    ProtectedBase() : protected_member(15) {}
    
protected:
    int getProtected() {
        return protected_member;
    }
};

struct ProtectedDerived : public ProtectedBase {
public:
    int accessBaseProtected() {
        return protected_member;  // Should be accessible in derived class
    }
    
    int callProtectedMethod() {
        return getProtected();  // Should be accessible in derived class
    }
};

int test_protected_access() {
    ProtectedDerived d;
    return d.accessBaseProtected() + d.callProtectedMethod();  // Expected: 30 (15 + 15)
}

// Test 3: Private members - NOT accessible in derived class
struct PrivateBase {
private:
    int private_member;
    
public:
    PrivateBase() : private_member(25) {}
    
    int getPrivate() {
        return private_member;
    }
};

struct PrivateDerived : public PrivateBase {
public:
    // Cannot access private_member directly - would be compile error
    // int accessBasePrivate() {
    //     return private_member;  // ERROR: private member not accessible
    // }
    
    int callPublicMethod() {
        return getPrivate();  // Can call public method
    }
};

int test_private_not_accessible() {
    PrivateDerived d;
    return d.callPublicMethod();  // Expected: 25
}

// Test 4: Protected inheritance - public becomes protected
struct PublicBase2 {
public:
    int public_member;
    
    PublicBase2() : public_member(30) {}
};

struct ProtectedInheritance : protected PublicBase2 {
public:
    int accessInherited() {
        return public_member;  // Accessible within derived class
    }
};

int test_protected_inheritance() {
    ProtectedInheritance d;
    // d.public_member would be ERROR - protected in derived
    return d.accessInherited();  // Expected: 30
}

// Test 5: Private inheritance - public becomes private
struct PublicBase3 {
public:
    int public_member;
    
    PublicBase3() : public_member(35) {}
};

struct PrivateInheritance : private PublicBase3 {
public:
    int accessInherited() {
        return public_member;  // Accessible within derived class
    }
};

int test_private_inheritance() {
    PrivateInheritance d;
    // d.public_member would be ERROR - private in derived
    return d.accessInherited();  // Expected: 35
}

// Test 6: Mixed access levels
struct MixedBase {
public:
    int public_val;
    
protected:
    int protected_val;
    
private:
    int private_val;
    
public:
    MixedBase() : public_val(5), protected_val(10), private_val(15) {}
    
    int getPrivate() {
        return private_val;
    }
};

struct MixedDerived : public MixedBase {
public:
    int sumAccessible() {
        return public_val + protected_val;  // Can access public and protected
        // Cannot access private_val directly
    }
    
    int sumAll() {
        return public_val + protected_val + getPrivate();  // Use public method for private
    }
};

int test_mixed_access() {
    MixedDerived d;
    return d.sumAccessible() + d.sumAll();  // Expected: 45 (15 + 30)
}

// Test 7: Multi-level inheritance access
struct Level1 {
protected:
    int level1_val;
    
public:
    Level1() : level1_val(7) {}
};

struct Level2 : public Level1 {
protected:
    int level2_val;
    
public:
    Level2() : level2_val(8) {}
};

struct Level3 : public Level2 {
public:
    int accessAll() {
        return level1_val + level2_val;  // Both should be accessible
    }
};

int test_multilevel_access() {
    Level3 d;
    return d.accessAll();  // Expected: 15 (7 + 8)
}

// Test 8: Access through pointers
struct BasePtr {
public:
    int public_val;
    
protected:
    int protected_val;
    
public:
    BasePtr() : public_val(12), protected_val(13) {}
};

struct DerivedPtr : public BasePtr {
public:
    int accessViaThis() {
        return public_val + protected_val;  // Access through implicit 'this'
    }
};

int test_pointer_access() {
    DerivedPtr d;
    DerivedPtr* ptr = &d;
    return ptr->accessViaThis();  // Expected: 25 (12 + 13)
}

