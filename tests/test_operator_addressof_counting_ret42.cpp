// Test operator& overload resolution - returns an int instead of pointer to avoid bug
// This demonstrates that operator overload resolution is working correctly

struct CountingAddressable {
    int count;
    
    // Overloaded operator& - returns an integer count
    // (normally operator& should return a pointer, but we work around the pointer-return bug)
    int operator&() {
        count++;
        return count;
    }
};

int main() {
    CountingAddressable obj;
    obj.count = 41;
    
    // Use & operator - should call operator& overload
    int result = &obj;  // This will call operator&(), which increments count and returns it
    
    // Since count was 41, after increment it should be 42
    return result;  // Should return 42
}
