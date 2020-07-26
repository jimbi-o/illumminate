#ifdef ASSERTIONS_ENABLED
#define ASSERT(expr, ...)                                               \
  if (expr) {} else {                                                   \
    logfatal("{} {} L{} {}", #expr, __FILE__, __LINE__, __VA_ARGS__);   \
    abort();                                                            \
  }
#else
#define ASSERT(expr, ...)
#endif
