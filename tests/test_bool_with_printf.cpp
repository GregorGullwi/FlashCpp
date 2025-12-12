// Test bool assignment and conditionals
extern "C" int printf(const char*, ...);

int main() {
   bool test = false;
   
   printf("test value: %d\n", test);
   printf("test ? 1 : 0 = %d\n", test ? 1 : 0);
   
   if (test) {
       printf("test is TRUE\n");
       return 1;
   } else {
       printf("test is FALSE\n");
       return 0;
   }
}
