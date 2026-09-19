// The alias target names a non-type parameter, so the alias cannot produce a
// type. The parser must reject the use with the structured
// NonTypeAliasTargetUnsupported diagnostic instead of treating the value
// argument as a type.
template <class T, int N>
using Pick = N;

int main() {
	Pick<char, 7> value = 0;
	(void)value;
	return 0;
}
