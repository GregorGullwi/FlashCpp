// Test global variables with initialization and usage

int global_initialized = 42;
int global_uninitialized;
int global_sum = 100;

int get_global_initialized() {
    return global_initialized;
}

int get_global_sum() {
    return global_initialized + global_sum;
}

int set_and_get_global() {
    global_uninitialized = 99;
    return global_uninitialized;
}

