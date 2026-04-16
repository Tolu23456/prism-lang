#ifndef FORMATTER_H
#define FORMATTER_H

char *prism_format_source(const char *source, char *errbuf, int errlen);
int prism_format_file(const char *path, int write_back, char *errbuf, int errlen);

#endif