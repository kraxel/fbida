#ifdef HAVE_LIBCURL
extern int curl_is_url(const char *url);
#else
static inline int curl_is_url(const char *url) { return 0; }
#endif
