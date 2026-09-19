// Alias templates are typedef-names and have no partial-specialization grammar
// ([temp.alias]). The parser must reject the template-id after the alias name
// with the structured AliasTemplateSpecializationForbidden diagnostic.
template <class T>
using Alias = T*;

template <class T>
using Alias<T*> = T;

int main() { return 0; }
