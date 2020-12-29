#ifdef ASSERTIONS_ENABLED
#define ASSERT(expr)                                      \
  if (expr) {} else {                                     \
    logfatal("{} {} L{} {}", #expr, __FILE__, __LINE__);  \
    abort();                                              \
  }                                                       \
  ((void)0)
#else
#define ASSERT(expr) ((void)0)
#endif
