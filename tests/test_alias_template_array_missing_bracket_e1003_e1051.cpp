// A malformed array bound in an alias template target must report the
// structured close-bracket diagnostic (1003) with its opening-bracket note
// (1051) instead of the generic "Expected ';'" error.
template <class T>
using Bad = T[3;

int main() { return 0; }
