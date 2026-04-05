int main() {
	char src[4] = {'a', 'b', 'c', '\0'};
	char dst[4] = {};

	__builtin_memcpy(dst, src, sizeof(src));

	return (dst[0] == 'a' && dst[1] == 'b' && dst[2] == 'c' && dst[3] == '\0') ? 0 : 1;
}
