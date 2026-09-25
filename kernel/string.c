#include <kernel/string.h>
void *memcpy(void *dst, const void *src, size_t n) {
  uint8_t *d = dst;
  const uint8_t *s = src;
  while (n--)
    *d++ = *s++;
  return dst;
}
void *memset(void *dst, int c, size_t n) {
  uint8_t *d = dst;
  while (n--)
    *d++ = (uint8_t)c;
  return dst;
}
size_t strlen(const char *s) {
  size_t n = 0;
  while (*s++)
    n++;
  return n;
}
int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}
static char low(char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int strcasecmp(const char *a, const char *b) {
  while (*a && low(*a) == low(*b)) {
    a++;
    b++;
  }
  return (unsigned char)low(*a) - (unsigned char)low(*b);
}
int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *x = a, *y = b;
  while (n--) {
    if (*x != *y)
      return *x - *y;
    x++;
    y++;
  }
  return 0;
}
char *strcpy(char *d, const char *s) {
  char *r = d;
  while ((*d++ = *s++))
    ;
  return r;
}
int strncmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i])
      return (unsigned char)a[i] - (unsigned char)b[i];
    if (a[i] == '\0')
      return 0;
  }
  return 0;
}

char *strncpy(char *d, const char *s, size_t n) {
  size_t i;
  for (i = 0; i < n && s[i]; i++)
    d[i] = s[i];
  for (; i < n; i++)
    d[i] = '\0';
  return d;
}

char *strrchr(const char *s, int c) {
  const char *last = NULL;
  while (*s) {
    if (*s == (char)c)
      last = s;
    s++;
  }
  if ((char)c == '\0')
    return (char *)s;
  return (char *)last;
}

char *strcat(char *dest, const char *src) {
  char *ptr = dest;
  while (*ptr != '\0')
    ptr++;
  while (*src != '\0')
    *ptr++ = *src++;
  *ptr = '\0';
  return dest;
}

char *strchr(const char *s, int c) {
  while (*s) {
    if (*s == (char)c)
      return (char *)s;
    s++;
  }
  return (c == '\0') ? (char *)s : NULL;
}
void *memmove(void *dest, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dest;
  const uint8_t *s = (const uint8_t *)src;

  if (d == s)
    return dest;

  if (d < s) {
    for (size_t i = 0; i < n; i++) {
      d[i] = s[i];
    }
  } else {
    for (size_t i = n; i > 0; i--) {
      d[i - 1] = s[i - 1];
    }
  }
  return dest;
}
