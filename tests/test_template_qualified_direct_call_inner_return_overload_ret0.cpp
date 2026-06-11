namespace QualifiedDirectCallInnerReturn {
	struct Big {
		long long first;
		long long second;
	};

	int make(int) {
		return 7;
	}

	Big make(long) {
		return Big{1, 2};
	}
}

int classify(int) {
	return 1;
}

int classify(QualifiedDirectCallInnerReturn::Big) {
	return 2;
}

template<typename T>
int runQualifiedDirectCall(T value) {
	return classify(QualifiedDirectCallInnerReturn::make(value));
}

int main() {
	return runQualifiedDirectCall<long>(0L) == 2 ? 0 : 1;
}
