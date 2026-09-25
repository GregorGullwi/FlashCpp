// A pointer cannot wrap a reference type, including when the reference comes
// through a typedef used as an alias-template default.
using Ref = int&;
template <class T = Ref*> using InvalidDefault = T;
InvalidDefault<> value;

int main() { return 0; }
