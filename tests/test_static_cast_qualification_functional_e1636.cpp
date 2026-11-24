// A functional cast performs direct-initialization ([expr.type.conv]), so the
// same implicit conversion sequence as static_cast applies. The alias spelling
// "DeepConst(source)" must not bypass the [conv.qual]/3 rule that rejects
// "unsigned short**" to "const unsigned short**".

using DeepConst = const unsigned short**;

int main() {
	unsigned short value = 17;
	unsigned short* pointer = &value;
	unsigned short** plain = &pointer;
	DeepConst qualified = DeepConst(plain);
	(void)qualified;
	return 0;
}
