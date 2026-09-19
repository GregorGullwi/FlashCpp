// A self-referential alias template is directly recursive. It must be rejected
// at declaration time with the structured RecursiveAliasTemplateInstantiation
// diagnostic instead of crashing the compiler during materialization.
template <class T>
using Self = Self<T>;

int main() { return 0; }
