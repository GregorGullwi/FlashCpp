// Test that -fno-access-control flag disables access control checking
// NOTE: This test must be compiled with -fno-access-control flag
// Without the flag, it will fail with "Cannot access private member" error
// The validation script doesn't pass this flag, so this test will fail in CI

class PrivateClass {
    int private_val;  // Private by default
    
public:
    PrivateClass() : private_val(100) {}
};

int test_access_private_with_flag() {
    PrivateClass obj;
    // This would normally be an error, but with -fno-access-control it should work
    return obj.private_val;  // Expected: 100
}


int main() {
    return test_access_private_with_flag();
}
