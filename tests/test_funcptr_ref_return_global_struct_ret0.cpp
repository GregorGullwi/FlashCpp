struct Big {
	long long a;
	long long b;
	long long c;
};

Big global{1, 2, 3};

Big& getGlobal() {
	return global;
}

int main() {
	Big& (*fp)() = getGlobal;
	fp().c = 7;
	return global.c - 7;
}
