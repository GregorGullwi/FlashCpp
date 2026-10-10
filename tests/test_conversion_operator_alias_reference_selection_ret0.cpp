struct ConversionSource {
	operator int() { return 17; }
	operator int() const { return 29; }
};

using ConversionSourceAlias = ConversionSource;
using ConversionSourceAliasChain = ConversionSourceAlias;

int consume(int value) { return value; }
int return_mutable(ConversionSourceAliasChain& source) { return source; }
int initialize_const(const ConversionSourceAlias& source) { int value = source; return value; }
int pass_mutable(ConversionSourceAliasChain& source) { return consume(source); }

int main() {
	ConversionSource source;
	const ConversionSource const_source;
	return return_mutable(source) == 17 && initialize_const(const_source) == 29 && pass_mutable(source) == 17 ? 0 : 1;
}
