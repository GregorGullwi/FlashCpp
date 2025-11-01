// Friend declarations tests for FlashCpp

// Test 1: Friend function accessing private members
class Box {
private:
    int width;
    int height;
    
    friend void setBoxDimensions(Box& b, int w, int h);
    
public:
    Box() : width(0), height(0) {}
    
    int getWidth() { return width; }
    int getHeight() { return height; }
};

void setBoxDimensions(Box& b, int w, int h) {
    b.width = w;      // Should compile - friend function
    b.height = h;     // Should compile - friend function
}

int test_friend_function() {
    Box b;
    setBoxDimensions(b, 10, 20);
    return b.getWidth() + b.getHeight();  // Expected: 30
}

// Test 2: Friend class accessing private members
class Storage {
private:
    int data;
    
    friend class Accessor;
    
public:
    Storage() : data(42) {}
};

class Accessor {
public:
    int getData(Storage& s) {
        return s.data;  // Should compile - friend class
    }
    
    void setData(Storage& s, int value) {
        s.data = value;  // Should compile - friend class
    }
};

int test_friend_class() {
    Storage s;
    Accessor a;
    int result = a.getData(s);  // Expected: 42
    a.setData(s, 100);
    return a.getData(s);  // Expected: 100
}

// Test 3: Multiple friend functions
class Counter {
private:
    int count;
    
    friend void increment(Counter& c);
    friend void decrement(Counter& c);
    
public:
    Counter() : count(0) {}
    int getCount() { return count; }
};

void increment(Counter& c) {
    c.count++;
}

void decrement(Counter& c) {
    c.count--;
}

int test_multiple_friends() {
    Counter c;
    increment(c);
    increment(c);
    decrement(c);
    return c.getCount();  // Expected: 1
}

// Test 4: Friend function with protected members
class Base {
protected:
    int protected_value;

    friend void accessProtected(Base& b);

public:
    Base() : protected_value(50) {}

    int getProtectedValue() { return protected_value; }
};

void accessProtected(Base& b) {
    b.protected_value = 75;  // Should compile - friend function
}

int test_friend_protected() {
    Base b;
    accessProtected(b);
    return b.getProtectedValue();  // Expected: 75
}

// Test 5: Friend class with inheritance
class Parent {
private:
    int secret;
    
    friend class FriendHelper;
    
public:
    Parent() : secret(99) {}
};

class FriendHelper {
public:
    int getSecret(Parent& p) {
        return p.secret;  // Should compile - friend class
    }
};

class Child : public Parent {
public:
    int tryGetSecret() {
        // This should NOT compile - Child is not friend of Parent
        // return secret;  // Error: private member
        return 0;
    }
};

int test_friend_inheritance() {
    Parent p;
    FriendHelper fh;
    return fh.getSecret(p);  // Expected: 99
}

// Test 6: Non-friend access should fail (compile-time error)
class PrivateData {
private:
    int secret;
    
public:
    PrivateData() : secret(42) {}
};

int test_non_friend_access() {
    PrivateData pd;
    // This should NOT compile:
    // return pd.secret;  // Error: private member
    return 0;  // Placeholder
}

// Test 7: Friend function calling member functions
class Calculator {
private:
    int result;
    
    friend void compute(Calculator& calc, int a, int b);
    
    void setResult(int r) { result = r; }
    
public:
    Calculator() : result(0) {}
    int getResult() { return result; }
};

void compute(Calculator& calc, int a, int b) {
    calc.setResult(a + b);  // Should compile - friend function
}

int test_friend_calling_member_function() {
    Calculator calc;
    compute(calc, 15, 25);
    return calc.getResult();  // Expected: 40
}

// Test 8: Friend function with multiple parameters
class Point {
private:
    int x;
    int y;
    
    friend void movePoint(Point& p, int dx, int dy);
    
public:
    Point() : x(0), y(0) {}
    int getX() { return x; }
    int getY() { return y; }
};

void movePoint(Point& p, int dx, int dy) {
    p.x += dx;
    p.y += dy;
}

int test_friend_multiple_params() {
    Point pt;
    movePoint(pt, 5, 10);
    return pt.getX() + pt.getY();  // Expected: 15
}

// Test 9: Friend class with multiple methods
class SecretVault {
private:
    int pin;
    int balance;
    
    friend class VaultManager;
    
public:
    SecretVault() : pin(1234), balance(1000) {}
};

class VaultManager {
public:
    int getPin(SecretVault& v) {
        return v.pin;  // Should compile
    }
    
    int getBalance(SecretVault& v) {
        return v.balance;  // Should compile
    }
    
    void setBalance(SecretVault& v, int amount) {
        v.balance = amount;  // Should compile
    }
};

int test_friend_class_multiple_methods() {
    SecretVault vault;
    VaultManager manager;
    int pin = manager.getPin(vault);
    int balance = manager.getBalance(vault);
    manager.setBalance(vault, 2000);
    return manager.getBalance(vault);  // Expected: 2000
}

// Test 10: Friend function in struct (default public)
struct PublicStruct {
    int value;
    
    friend void modifyStruct(PublicStruct& s);
    
    PublicStruct() : value(0) {}
};

void modifyStruct(PublicStruct& s) {
    s.value = 123;
}

int test_friend_in_struct() {
    PublicStruct ps;
    modifyStruct(ps);
    return ps.value;  // Expected: 123
}

