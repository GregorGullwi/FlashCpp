// Comprehensive test for named unions in structs
// Expected return: 100

struct DataContainer {
    // Named union as a member
    union Value {
        int i;
        float f;
        char c;
    } value;
    
    // Another named union for comparison
    union Flags {
        int all;
        float fval;
    } flags;
    
    int regular_member;
};

int main() {
    DataContainer dc;
    
    // Test accessing named union members
    dc.value.i = 42;
    dc.flags.all = 58;
    dc.regular_member = 10;
    
    // Return sum: 42 + 58 = 100
    return dc.value.i + dc.flags.all;
}
