template <typename T>
struct Reader;

class Vault {
	int secret;
	friend struct Reader<int*>;

public:
	Vault() : secret(42) {}
};

template <typename T>
struct Reader {
	int read(Vault&) { return 0; }
};

template <typename T>
struct Reader<T*> {
	int read(Vault& vault) { return vault.secret; }
};

int main() {
	Vault vault;
	Reader<char*> reader;
	return reader.read(vault);
}
