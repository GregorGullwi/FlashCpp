// An ordered dereference must reach sema's overload resolver so an incompatible
// candidate is rejected with the standard no-viable-function diagnostic.
int consume_ordered(double (*(*(*param))[3])[4]) {
	return param == nullptr ? 1 : 0;
}

int main() {
	int (*(*(*rows)[2])[3])[4] = nullptr;
	return consume_ordered(*rows);
}
