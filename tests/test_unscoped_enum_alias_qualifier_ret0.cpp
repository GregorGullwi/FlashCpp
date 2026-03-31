struct Container {
	enum Status { Ok = 3,
				  Fail = 9 };
	using AliasStatus = Status;
};

int main() {
	Container::AliasStatus alias_ok = Container::AliasStatus::Ok;
	return alias_ok == Container::Status::Ok ? 0 : 1;
}
