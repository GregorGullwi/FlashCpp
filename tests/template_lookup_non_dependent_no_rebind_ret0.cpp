int lookup_probe(long) {
	return 1;
}

template <typename T>
int use_definition_lookup(T) {
	return lookup_probe(0);
}

int lookup_probe(int) {
	return 2;
}

int main() {
	return use_definition_lookup(0) - 1;
}
