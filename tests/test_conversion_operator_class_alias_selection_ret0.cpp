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
int return_alias_value(ConversionSourceAliasChain source) { return source; }
int initialize_alias_value(ConversionSourceAlias source) { int value = source; return value; }
int pass_alias_value(ConversionSourceAlias source) { return consume(source); }

int main() {
	ConversionSource source;
	const ConversionSource const_source;
	int result = 0;
	result |= return_mutable(source) == 17 ? 0 : 1;
	result |= initialize_const(const_source) == 29 ? 0 : 2;
	result |= pass_mutable(source) == 17 ? 0 : 4;
	result |= return_alias_value(source) == 17 ? 0 : 8;
	result |= initialize_alias_value(source) == 17 ? 0 : 16;
	result |= pass_alias_value(source) == 17 ? 0 : 32;
	return result;
}
