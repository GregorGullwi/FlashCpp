// An array of function pointers declared inline (not through a typedef) is a
// projectable ordered declarator spine whose element is a callable pointer.
// A global initializer stores one absolute-address relocation per element and a
// local initializer stores one pointer per element; each element is callable.
struct Payload {
	int value;
	int tag;
};

int inc(int x) { return x + 1; }
int dec(int x) { return x - 1; }
int sum_payload(Payload p) { return p.value + p.tag; }

int (*g_ints[2])(int) = {&inc, &dec};
int (*g_payloads[2])(Payload) = {&sum_payload, &sum_payload};

template <class T> T negate(T value) { return static_cast<T>(-value); }
int (*g_negate[2])(int) = {&negate<int>, &negate<int>};

int main() {
	int (*local_ints[2])(int) = {&inc, &dec};
	int (*local_payloads[2])(Payload) = {&sum_payload, &sum_payload};
	const bool global_ok = g_ints[0](4) == 5 && g_ints[1](4) == 3;
	const bool local_ok = local_ints[0](10) == 11 && local_ints[1](10) == 9;
	const bool payload_ok = g_payloads[0](Payload{3, 4}) == 7 && local_payloads[1](Payload{5, 6}) == 11;
	const bool template_ok = g_negate[0](8) == -8;
	return global_ok && local_ok && payload_ok && template_ok ? 0 : 1;
}
