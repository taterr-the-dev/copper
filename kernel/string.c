#include <kernel/string.h>

void *memcpy(void *dst, const void *src, size_t n) {
  uint8_t *d = dst;
  const uint8_t *s = src;
  
  if (!dst || !src || n == 0)
    return dst;
  
  if (!((uintptr_t)d & 7) && !((uintptr_t)s & 7) && n >= 8) {
    uint64_t *d64 = (uint64_t *)d;
    const uint64_t *s64 = (const uint64_t *)s;
    while (n >= 8) {
      *d64++ = *s64++;
      n -= 8;
    }
    d = (uint8_t *)d64;
    s = (const uint8_t *)s64;
  }
  
  while (n--)
    *d++ = *s++;
  return dst;
}

void *memset(void *dst, int c, size_t n) {
  uint8_t *d = dst;
  
  if (!dst || n == 0)
    return dst;
  
  uint8_t val = (uint8_t)c;
  
  if (!((uintptr_t)d & 7) && n >= 8) {
    uint64_t pattern = val | (val << 8) | (val << 16) | (val << 24);
    pattern |= (pattern << 16) << 16;
    uint64_t *d64 = (uint64_t *)d;
    while (n >= 8) {
      *d64++ = pattern;
      n -= 8;
    }
    d = (uint8_t *)d64;
  }
  
  while (n--)
    *d++ = val;
  return dst;
}

void *memmove(void *dest, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dest;
  const uint8_t *s = (const uint8_t *)src;

  if (!dest || !src || n == 0)
    return dest;
  
  if (d == s)
    return dest;

  if (d < s) {
    for (size_t i = 0; i < n; i++)
      d[i] = s[i];
  } else {
    for (size_t i = n; i > 0; i--)
      d[i - 1] = s[i - 1];
  }
  return dest;
}

int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *x = a, *y = b;
  
  if (!a || !b)
    return 0;
  
  while (n--) {
    if (*x != *y)
      return *x - *y;
    x++;
    y++;
  }
  return 0;
}

void *memchr(const void *s, int c, size_t n) {
  const unsigned char *p = s;
  
  if (!s || n == 0)
    return NULL;
  
  while (n--) {
    if (*p == (unsigned char)c)
      return (void *)p;
    p++;
  }
  return NULL;
}

void bzero(void *s, size_t n) {
  memset(s, 0, n);
}

void bcopy(const void *src, void *dest, size_t n) {
  memmove(dest, src, n);
}

size_t strlen(const char *s) {
  size_t n = 0;
  if (!s)
    return 0;
  while (*s++)
    n++;
  return n;
}

size_t strnlen(const char *s, size_t maxlen) {
  size_t n = 0;
  if (!s)
    return 0;
  while (n < maxlen && *s++)
    n++;
  return n;
}

int strcmp(const char *a, const char *b) {
  if (!a || !b)
    return (a == b) ? 0 : (a ? 1 : -1);
  
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
  if (!a || !b)
    return (a == b) ? 0 : (a ? 1 : -1);
  
  if (n == 0)
    return 0;
  
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i])
      return (unsigned char)a[i] - (unsigned char)b[i];
    if (a[i] == '\0')
      return 0;
  }
  return 0;
}

int strcasecmp(const char *a, const char *b) {
  if (!a || !b)
    return (a == b) ? 0 : (a ? 1 : -1);
  
  while (*a && tolower(*a) == tolower(*b)) {
    a++;
    b++;
  }
  return (unsigned char)tolower(*a) - (unsigned char)tolower(*b);
}

int strncasecmp(const char *a, const char *b, size_t n) {
  if (!a || !b)
    return (a == b) ? 0 : (a ? 1 : -1);
  
  if (n == 0)
    return 0;
  
  for (size_t i = 0; i < n; i++) {
    if (tolower(a[i]) != tolower(b[i]))
      return (unsigned char)tolower(a[i]) - (unsigned char)tolower(b[i]);
    if (a[i] == '\0')
      return 0;
  }
  return 0;
}

char *strcpy(char *d, const char *s) {
  char *r = d;
  if (!d || !s)
    return d;
  while ((*d++ = *s++))
    ;
  return r;
}

char *strncpy(char *d, const char *s, size_t n) {
  size_t i;
  if (!d || !s || n == 0)
    return d;
  
  for (i = 0; i < n && s[i]; i++)
    d[i] = s[i];
  for (; i < n; i++)
    d[i] = '\0';
  return d;
}

size_t strlcpy(char *dst, const char *src, size_t siz) {
  const char *s = src;
  size_t n = siz;
  
  if (!dst || !src)
    return 0;
  
  if (n != 0) {
    while (--n != 0) {
      if ((*dst++ = *s++) == '\0')
        break;
    }
  }
  
  if (n == 0) {
    if (siz != 0)
      *dst = '\0';
    while (*s++)
      ;
  }
  
  return s - src - 1;
}

char *strcat(char *dest, const char *src) {
  char *ptr = dest;
  if (!dest || !src)
    return dest;
  while (*ptr != '\0')
    ptr++;
  while (*src != '\0')
    *ptr++ = *src++;
  *ptr = '\0';
  return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
  char *ptr = dest;
  if (!dest || !src || n == 0)
    return dest;
  
  while (*ptr != '\0')
    ptr++;
  
  while (n-- && *src != '\0')
    *ptr++ = *src++;
  
  *ptr = '\0';
  return dest;
}

size_t strlcat(char *dst, const char *src, size_t siz) {
  char *d = dst;
  const char *s = src;
  size_t n = siz;
  size_t dlen;
  
  if (!dst || !src)
    return 0;
  
  while (n-- != 0 && *d != '\0')
    d++;
  dlen = d - dst;
  n = siz - dlen;
  
  if (n == 0)
    return dlen + strlen(s);
  
  while (*s != '\0') {
    if (n != 1) {
      *d++ = *s;
      n--;
    }
    s++;
  }
  *d = '\0';
  
  return dlen + (s - src);
}

char *strchr(const char *s, int c) {
  if (!s)
    return NULL;
  while (*s) {
    if (*s == (char)c)
      return (char *)s;
    s++;
  }
  return (c == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
  const char *last = NULL;
  if (!s)
    return NULL;
  while (*s) {
    if (*s == (char)c)
      last = s;
    s++;
  }
  if ((char)c == '\0')
    return (char *)s;
  return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
  size_t needle_len;
  
  if (!haystack || !needle)
    return NULL;
  
  if (*needle == '\0')
    return (char *)haystack;
  
  needle_len = strlen(needle);
  
  for (; (haystack = strchr(haystack, *needle)) != NULL; haystack++) {
    if (strncmp(haystack, needle, needle_len) == 0)
      return (char *)haystack;
  }
  
  return NULL;
}

char *strpbrk(const char *s, const char *accept) {
  if (!s || !accept)
    return NULL;
  
  while (*s) {
    for (const char *a = accept; *a; a++) {
      if (*s == *a)
        return (char *)s;
    }
    s++;
  }
  return NULL;
}

size_t strspn(const char *s, const char *accept) {
  const char *p;
  if (!s || !accept)
    return 0;
  
  for (p = s; *p; p++) {
    const char *a;
    for (a = accept; *a; a++) {
      if (*p == *a)
        break;
    }
    if (*a == '\0')
      return p - s;
  }
  return p - s;
}

size_t strcspn(const char *s, const char *reject) {
  const char *p;
  if (!s || !reject)
    return s ? strlen(s) : 0;
  
  for (p = s; *p; p++) {
    const char *r;
    for (r = reject; *r; r++) {
      if (*p == *r)
        return p - s;
    }
  }
  return p - s;
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
  char *token;
  
  if (!delim || !saveptr)
    return NULL;
  
  if (str == NULL)
    str = *saveptr;
  
  if (str == NULL)
    return NULL;
  
  str += strspn(str, delim);
  if (*str == '\0') {
    *saveptr = NULL;
    return NULL;
  }
  
  token = str;
  str = strpbrk(token, delim);
  if (str == NULL) {
    *saveptr = NULL;
  } else {
    *str = '\0';
    *saveptr = str + 1;
  }
  
  return token;
}

static char *strtok_last = NULL;

char *strtok(char *str, const char *delim) {
  return strtok_r(str, delim, &strtok_last);
}
