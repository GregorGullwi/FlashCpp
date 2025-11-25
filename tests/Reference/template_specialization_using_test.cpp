// Test file for template specialization with using declarations in namespaces
// This matches the pattern in <cstddef>

namespace std {
  template<typename _IntegerType> struct __byte_operand { };
  template<> struct __byte_operand<bool> { using __type = int; };
  template<> struct __byte_operand<char> { using __type = int; };
}

int main() { return 0; }
