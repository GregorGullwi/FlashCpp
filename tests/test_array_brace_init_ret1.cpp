// Test: Array brace initialization in different scopes

extern "C" int printf(const char* fmt, ...);

// Global array with brace initialization
int global_arr[] = {100, 200, 300};

int main() {
    // Local array with brace initialization
    int local_arr[] = {10, 20, 30};
    
    // Array with explicit size
    int sized_arr[3] = {1, 2, 3};
    
    // Check global array
    int g_sum = global_arr[0] + global_arr[1] + global_arr[2];
    printf("global: %d + %d + %d = %d (expected 600)\n", 
           global_arr[0], global_arr[1], global_arr[2], g_sum);
    
    // Check local array
    int l_sum = local_arr[0] + local_arr[1] + local_arr[2];
    printf("local: %d + %d + %d = %d (expected 60)\n",
           local_arr[0], local_arr[1], local_arr[2], l_sum);
    
    // Check sized array
    int s_sum = sized_arr[0] + sized_arr[1] + sized_arr[2];
    printf("sized: %d + %d + %d = %d (expected 6)\n",
           sized_arr[0], sized_arr[1], sized_arr[2], s_sum);
    
    return (g_sum == 600 && l_sum == 60 && s_sum == 6) ? 0 : 1;
}
