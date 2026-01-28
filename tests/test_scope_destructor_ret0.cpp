// Test that destructors are called when a scope block ends
// and that they are called in reverse order of construction

// Manual declaration of printf
extern "C" int printf(const char* format, ...);

struct ScopedObject {
    int id;
    
    ScopedObject(int identifier) : id(identifier) {
        printf("Constructor: obj%d\n", id);
    }
    
    ~ScopedObject() {
        printf("Destructor: obj%d\n", id);
    }
};

int main() {
    printf("Starting main\n");
    
    // Test 1: Basic scope with two objects
    {
        printf("Entering scope\n");
        ScopedObject obj1(1);
        ScopedObject obj2(2);
        printf("Leaving scope\n");
    }
    
    printf("\n");
    
    // Test 2: Multiple scopes to simulate loop iterations
    printf("Simulating loop iterations\n");
	for (int i = 0; i < 2; i++)
    {
        printf("Iteration 0\n");
        ScopedObject obj(10);
        printf("End of iteration %d\n", i);
    }
    printf("Loop simulation finished\n");
    
    printf("\nExiting main\n");
    return 0;
}
