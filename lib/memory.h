
extern void *xcalloc(size_t nmemb, size_t size);
extern void *xmalloc(size_t size);
extern void *xrealloc(void *ptr, size_t size);
extern char *xstrdup(const char *s);
extern const char *xfreenullstr;

#define xfree(P) { if ((P) == NULL) { errprintf(xfreenullstr); abort(); } free((P)); (P) = NULL; }
