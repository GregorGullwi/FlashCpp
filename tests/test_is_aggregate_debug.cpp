// Debug test for __is_aggregate compiler intrinsic - detailed
extern "C" int printf(const char*, ...);

struct SimpleAggregate {
   int x;
   double y;
};

struct WithConstructor {
   int x;
   WithConstructor() : x(0) { }
};

int main() {
   // Arrays can be tested at compile time
   constexpr bool test_array = __is_aggregate(int[5]);
   constexpr bool test_int = __is_aggregate(int);
   
   // Struct aggregates need runtime evaluation
   bool test_simple = __is_aggregate(SimpleAggregate);
   bool test_with_ctor = __is_aggregate(WithConstructor);
   
   printf("SimpleAggregate is aggregate: %d (expected: 1)\n", test_simple);
   printf("WithConstructor is aggregate: %d (expected: 0)\n", test_with_ctor);
   printf("int is aggregate: %d (expected: 0)\n", test_int);
   printf("int[5] is aggregate: %d (expected: 1)\n", test_array);
   
   printf("\nChecking test 1 (SimpleAggregate should be aggregate)...\n");
   if (!test_simple) {
       printf("FAILED: SimpleAggregate is not aggregate\n");
       return 1;
   }
   printf("PASSED\n");
   
   printf("\nChecking test 2 (WithConstructor should NOT be aggregate)...\n");
   if (test_with_ctor) {
       printf("FAILED: WithConstructor is aggregate\n");
       return 2;
   }
   printf("PASSED\n");
   
   printf("\nChecking test 3 (int should NOT be aggregate)...\n");
   if (test_int) {
       printf("FAILED: int is aggregate\n");
       return 3;
   }
   printf("PASSED\n");
   
   printf("\nChecking test 4 (int[5] should be aggregate)...\n");
   if (!test_array) {
       printf("FAILED: int[5] is not aggregate\n");
       return 4;
   }
   printf("PASSED\n");
   
   printf("\nAll tests passed!\n");
   return 0;
}
