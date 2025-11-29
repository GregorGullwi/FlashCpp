// Test that classes have private default access (unlike structs which are public)

// Test 1: Class members are private by default
class PrivateByDefault {
    int private_val;  // Private by default in class
    
public:
    PrivateByDefault() : private_val(42) {}
    
    int getPrivate() {
        return private_val;  // Can access within same class
    }
};

int test_class_private_default() {
    PrivateByDefault obj;
    // obj.private_val would be ERROR - private by default
    return obj.getPrivate();  // Expected: 42
}

// Test 2: Struct members are public by default
struct PublicByDefault {
    int public_val;  // Public by default in struct
    
    PublicByDefault() : public_val(55) {}
};

int test_struct_public_default() {
    PublicByDefault obj;
    return obj.public_val;  // Expected: 55 - accessible because public by default
}

// Test 3: Class with explicit public section
class MixedClass {
    int private_val;  // Private by default
    
public:
    int public_val;   // Explicitly public
    
    MixedClass() : private_val(10), public_val(20) {}
    
    int getPrivate() {
        return private_val;
    }
};

int test_mixed_class() {
    MixedClass obj;
    // obj.private_val would be ERROR
    return obj.public_val + obj.getPrivate();  // Expected: 30 (20 + 10)
}

// Test 4: Class inheritance - private by default
class BaseClass {
    int base_private;
    
public:
    int base_public;
    
    BaseClass() : base_private(7), base_public(8) {}
    
    int getBasePrivate() {
        return base_private;
    }
};

class DerivedClass : public BaseClass {
public:
    int accessBase() {
        // Cannot access base_private - it's private
        return base_public;  // Can access public member
    }
};

int test_class_inheritance() {
    DerivedClass obj;
    return obj.accessBase() + obj.getBasePrivate();  // Expected: 15 (8 + 7)
}

// Test 5: Class with protected members
class ProtectedClass {
protected:
    int protected_val;
    
public:
    ProtectedClass() : protected_val(33) {}
};

class DerivedProtected : public ProtectedClass {
public:
    int accessProtected() {
        return protected_val;  // Can access protected in derived class
    }
};

int test_protected_in_class() {
    DerivedProtected obj;
    // obj.protected_val would be ERROR - protected
    return obj.accessProtected();  // Expected: 33
}

