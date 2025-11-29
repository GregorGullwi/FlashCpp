// Test covariant return types in virtual functions
// Note: Using pointers to existing objects instead of new/delete

// Test 1: Basic covariant return with pointers
struct Animal {
    int type;

    Animal() : type(1) {}

    virtual Animal* getSelf() {
        return this;
    }

    virtual ~Animal() {}
};

struct Dog : public Animal {
    int breed;

    Dog() : breed(5) {
        type = 2;
    }

    // Covariant return: returns Dog* instead of Animal*
    virtual Dog* getSelf() {
        return this;
    }
};

int test_basic_covariant() {
    Dog d;
    d.type = 10;
    d.breed = 20;

    Dog* ptr = d.getSelf();  // Direct call returns Dog*
    return ptr->type + ptr->breed;  // Expected: 30
}

// Test 2: Covariant return through base pointer
int test_covariant_via_base_pointer() {
    Dog d;
    d.type = 15;
    d.breed = 25;

    Animal* base = &d;
    Animal* ptr = base->getSelf();  // Virtual call, returns Dog* as Animal*

    return ptr->type;  // Expected: 15
}

// Test 3: Multi-level covariant returns
struct Bird : public Animal {
    int can_fly;

    Bird() : can_fly(1) {
        type = 3;
    }

    virtual Bird* getSelf() {
        return this;
    }
};

struct Parrot : public Bird {
    int talk_count;

    Parrot() : talk_count(9) {
        type = 4;
        can_fly = 1;
    }

    virtual Parrot* getSelf() {
        return this;
    }
};

int test_multilevel_covariant() {
    Parrot p;
    p.type = 7;
    p.talk_count = 3;

    Parrot* ptr = p.getSelf();
    return ptr->type + ptr->talk_count;  // Expected: 10
}

// Test 4: Covariant return with references
struct Base {
    int value;
    
    Base() : value(100) {}
    
    virtual Base& getSelf() {
        return *this;
    }
    
    virtual ~Base() {}
};

struct Derived : public Base {
    int extra;
    
    Derived() : extra(200) {
        value = 150;
    }
    
    virtual Derived& getSelf() {
        return *this;
    }
};

int test_covariant_reference() {
    Derived d;
    d.value = 50;
    d.extra = 75;
    
    Derived& ref = d.getSelf();
    return ref.value + ref.extra;  // Expected: 125
}

// Test 5: Covariant return via base reference
int test_covariant_reference_via_base() {
    Derived d;
    d.value = 30;
    d.extra = 40;
    
    Base& base_ref = d;
    Base& result = base_ref.getSelf();  // Virtual call
    
    return result.value;  // Expected: 30
}

// Test 6: Covariant with multiple inheritance levels
struct Vehicle {
    int wheels;

    Vehicle() : wheels(0) {}

    virtual Vehicle* getVehicle() {
        return this;
    }

    virtual ~Vehicle() {}
};

struct Car : public Vehicle {
    int doors;

    Car() : doors(4) {
        wheels = 4;
    }

    virtual Car* getVehicle() {
        return this;
    }
};

int test_vehicle_covariant() {
    Car c;
    c.wheels = 4;
    c.doors = 2;

    Car* ptr = c.getVehicle();
    return ptr->wheels + ptr->doors;  // Expected: 6
}

// Test 7: Covariant with const pointers
struct ConstBase {
    int val;
    
    ConstBase() : val(10) {}
    
    virtual const ConstBase* getConst() const {
        return this;
    }
    
    virtual ~ConstBase() {}
};

struct ConstDerived : public ConstBase {
    int extra_val;
    
    ConstDerived() : extra_val(20) {
        val = 15;
    }
    
    virtual const ConstDerived* getConst() const {
        return this;
    }
};

int test_covariant_const_pointer() {
    ConstDerived cd;
    cd.val = 8;
    cd.extra_val = 12;
    
    const ConstDerived* ptr = cd.getConst();
    return ptr->val + ptr->extra_val;  // Expected: 20
}

