// Alias templates are typedef-names and cannot be explicitly specialized
// ([temp.alias]). The parser must reject the template-id after the alias name
// with the structured AliasTemplateSpecializationForbidden diagnostic.
template <class T>
using Alias = T*;

template <>
using Alias<int> = int;

int main() { return 0; }
