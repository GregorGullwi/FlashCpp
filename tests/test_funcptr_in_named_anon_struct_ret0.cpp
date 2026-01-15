// Test: Function pointer members in named anonymous struct/union patterns
// This tests patterns like those found in <csignal> system headers

typedef union {
    int i;
} SignalValue;

typedef struct {
    union {
        int value;
    } attr_t;
} ThreadAttr;

// Handler function that matches the sigevent callback signature
void mySignalHandler(SignalValue sv) {
    // Just return without doing anything - we're testing the call mechanism
}

// Pattern from sigevent_t (csignal)
typedef struct sigevent
{
    SignalValue sigev_value;
    int sigev_signo;
    int sigev_notify;
    
    union
    {
        int _pad[16];
        int _tid;
        
        // Named anonymous struct with function pointer member
        struct
        {
            void (*_function) (SignalValue);  // Function pointer member
            ThreadAttr *_attribute;           // Pointer member
        } _sigev_thread;
    } _sigev_un;
} sigevent_t;

// Pattern from sigaction (sigaction.h)
typedef void (*signal_handler_t)(int);

struct siginfo_t {
    int si_signo;
};

// Handler function for sigaction-style callback
void mySimpleHandler(int sig) {
    // Just return without doing anything
}

struct sigaction
{
    // Named anonymous union with function pointer member
    union
    {
        signal_handler_t sa_handler;
        void (*sa_sigaction) (int, siginfo_t *, void *);  // Function pointer member
    }
    __sigaction_handler;
    
    int sa_mask;
    int sa_flags;
    
    // Regular function pointer member
    void (*sa_restorer) (void);
};

int main() {
    // Test sigevent_t pattern - assign and call function pointer
    sigevent_t se;
    se.sigev_signo = 1;
    se._sigev_un._sigev_thread._function = mySignalHandler;
    
    SignalValue sv;
    sv.i = 42;
    se._sigev_un._sigev_thread._function(sv);  // Call through nested member access
    
    // Test sigaction pattern - assign and call function pointer
    sigaction sa;
    sa.sa_flags = 0;
    sa.__sigaction_handler.sa_handler = mySimpleHandler;
    sa.__sigaction_handler.sa_handler(99);  // Call through union member
    
    return 0;
}
