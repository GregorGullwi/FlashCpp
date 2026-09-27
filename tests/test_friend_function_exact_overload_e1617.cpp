class Vault;

class Vault {
	int secret;

public:
	Vault() : secret(42) {}
	friend int inspect(Vault& vault, int selector);
};

int inspect(Vault& vault, int selector) {
	return vault.secret + selector;
}

int inspect(Vault& vault, double selector) {
	return vault.secret + static_cast<int>(selector);
}

int main() {
	Vault vault;
	return inspect(vault, 0);
}
