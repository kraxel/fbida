/* these are "for free", the desktop file stuff needs this anyway ... */
int utf8_to_locale(char *src, char *dst, size_t max);
int locale_to_utf8(char *src, char *dst, size_t max);

/* handle desktop files */
int desktop_read_entry(char *filename, char *entry, char *dest, size_t max);
int desktop_write_entry(char *filename, char *type, char *entry, char *value);
