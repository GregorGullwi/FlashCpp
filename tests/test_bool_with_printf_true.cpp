// Test bool assignment and conditionals with true
extern "C" int printf(const char*, ...);

int main() {
   bool test = true;
   
   printf("test value: %d\n", test);
   printf("test ? 1 : 0 = %d\n", test ? 1 : 0);
   
   if (test) {
       printf("test is TRUE\n");
       return 0;  // Success when true
   } else {
       printf("test is FALSE\n");
       return 1;
   }
}
