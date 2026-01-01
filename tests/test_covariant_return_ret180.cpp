// Test covariant return types in virtual functions
// Expected return: 180
// Tests basic covariant pointers, multi-level inheritance, and reference returns

// Test 1: Basic covariant return with pointers (30)
struct Animal {
    int type;
    Animal() : type(1) {}
    virtual Animal* getSelf() { return this; }
    virtual ~Animal() {}
};

struct Dog : public Animal {
    int breed;
    Dog() : breed(5) { type = 2; }
    virtual Dog* getSelf() { return this; }
};

int test_basic_covariant() {
    Dog d;
    d.type = 10;
    d.breed = 20;
    Dog* ptr = d.getSelf();
    return ptr->type + ptr->breed;  // 30
}

// Test 2: Covariant through base pointer (15)
int test_via_base_pointer() {
    Dog d;
    d.type = 15;
    Animal* base = &d;
    Animal* ptr = base->getSelf();
    return ptr->type;  // 15
}

// Test 3: Multi-level covariant (10)
struct Bird : public Animal {
    int can_fly;
    Bird() : can_fly(1) { type = 3; }
    virtual Bird* getSelf() { return this; }
};

struct Parrot : public Bird {
    int talk_count;
    Parrot() : talk_count(9) { type = 4; can_fly = 1; }
    virtual Parrot* getSelf() { return this; }
};

int test_multilevel() {
    Parrot p;
    p.type = 7;
    p.talk_count = 3;
    Parrot* ptr = p.getSelf();
    return ptr->type + ptr->talk_count;  // 10
}

// Test 4: Covariant with references (125)
struct Base {
    int value;
    Base() : value(100) {}
    virtual Base& getSelf() { return *this; }
    virtual ~Base() {}
};

struct Derived : public Base {
    int extra;
    Derived() : extra(200) { value = 150; }
    virtual Derived& getSelf() { return *this; }
};

int test_reference() {
    Derived d;
    d.value = 50;
    d.extra = 75;
    Derived& ref = d.getSelf();
    return ref.value + ref.extra;  // 125
}

int main() {
    int result = 0;
    result += test_basic_covariant();      // 30
    result += test_via_base_pointer();     // 15
    result += test_multilevel();           // 10
    result += test_reference();            // 125
    // Total: 180
    return result;
}
