class Vault;

namespace trusted {
class Reader;
}

class Vault {
	int secret;

public:
	Vault() : secret(42) {}
	friend class trusted::Reader;
};

namespace trusted {
class Reader {
public:
	int read(const Vault& vault) const {
		return vault.secret;
	}
};
}

int main() {
	Vault vault;
	trusted::Reader reader;
	return reader.read(vault);
}
