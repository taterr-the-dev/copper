#ifndef _KERNEL_STRING_H
#define _KERNEL_STRING_H

#include <kernel/types.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);

void bzero(void *s, size_t n);
void bcopy(const void *src, void *dest, size_t n);

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);

int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);

char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
size_t strlcpy(char *dst, const char *src, size_t siz);

char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
size_t strlcat(char *dst, const char *src, size_t siz);

char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strpbrk(const char *s, const char *accept);

size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);

char *strtok(char *str, const char *delim);
char *strtok_r(char *str, const char *delim, char **saveptr);

static inline int tolower(int c) {
  return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static inline int toupper(int c) {
  return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

#endif
