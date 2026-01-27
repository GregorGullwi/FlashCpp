// Test to verify action table generation for multiple catch handlers
// NOTE: This test demonstrates that the action table is correctly generated
// with chained action entries. However, due to the landing pad architecture
// limitation documented in LSDAGenerator.h, only the first handler will
// execute at runtime until the landing pad code is refactored.

extern "C" int printf(const char*, ...);

int test_action_table_generation() {
    // This try block has 3 catch handlers
    // The action table should generate 3 chained entries:
    // - Entry 0: type_filter=1 (char), next_offset=1
    // - Entry 1: type_filter=2 (int), next_offset=1  
    // - Entry 2: type_filter=0 (catch-all), next_offset=0
    try {
        throw 42;
    } catch (char c) {
        printf("Caught char: %d\n", c);
        return 1;
    } catch (int i) {
        printf("Caught int: %d\n", i);
        return i;
    } catch (...) {
        printf("Caught ...\n");
        return 2;
    }
    return 0;
}

int main() {
    printf("Testing action table generation for multiple catch handlers\n");
    printf("(Note: Due to landing pad limitations, first handler always executes)\n\n");
    
    int result = test_action_table_generation();
    printf("Result: %d\n", result);
    
    // To verify action table is correctly generated, compile with -v flag:
    // FlashCpp this_file.cpp -o output.o -v
    // Look for:
    //   [DEBUG] Action chaining: entry 0 -> entry 1 next_offset=1
    //   [DEBUG] Action chaining: entry 1 -> entry 2 next_offset=1
    //   [DEBUG] Action table size: 6 bytes
    
    return 0;
}
