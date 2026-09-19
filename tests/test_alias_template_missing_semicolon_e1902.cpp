// A missing semicolon after an alias template declaration must report the
// structured MissingSemicolon diagnostic instead of an unstructured error.
template <class T>
using Identity = T

int main() { return 0; }
