// Test that duplicate variable names in function scope are properly detected
int main()
{
  int sum = 0;
  int sum = 1;  // Error: redefinition of 'sum'
  return sum;
}
