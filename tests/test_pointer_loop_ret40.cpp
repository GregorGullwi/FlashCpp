struct P
{
	int x{10};
	char y = {1};
	float z{};
	double w{3};
	int* p = nullptr;
    P() = default;
};

int main() {
    int arr[2];
    arr[0] = 10;
    arr[1] = 20;
    
    int* begin = &arr[0];
    int* end = &arr[2];
    
    int sum = 0;
    while (begin != end) {
        sum = sum + *begin;
        begin = begin + 1;
    }
	
	P p[3]{};
	P* pp = &p[0];
	for (int i = 0; i < (sizeof(p) / sizeof(P)); ++i, ++pp)
	{
		pp->p = &p[i].x;
	}
    
	return sum + *(p[0].p);  // Expected: 40
}
