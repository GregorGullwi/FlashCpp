// Deleted move constructor should be diagnosed for same-type xvalue direct initialization.

// Diagnostic regression for deleted move constructor.
struct NoMove {
	NoMove() = default;
	NoMove(const NoMove&) = default;
	NoMove(NoMove&&) = delete;
};

int main() {
	NoMove source;
	NoMove moved(static_cast<NoMove&&>(source));
	return 0;
}
