#include "text.h"

size_t tk_wlen(const wchar_t *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

size_t tk_slen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

size_t utf16_to_utf8(const wchar_t *s, size_t n, char *out) {
    size_t bytes = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t c = (uint16_t)s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && (uint16_t)s[i + 1] >= 0xDC00 && (uint16_t)s[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + ((uint16_t)s[i + 1] - 0xDC00);
            i++;
        } else if (c >= 0xD800 && c <= 0xDFFF) {
            c = 0xFFFD;
        }
        unsigned char b[4];
        size_t k;
        if (c < 0x80) b[0] = (unsigned char)c, k = 1;
        else if (c < 0x800) b[0] = (unsigned char)(0xC0 | c >> 6), b[1] = (unsigned char)(0x80 | (c & 63)), k = 2;
        else if (c < 0x10000)
            b[0] = (unsigned char)(0xE0 | c >> 12), b[1] = (unsigned char)(0x80 | (c >> 6 & 63)),
            b[2] = (unsigned char)(0x80 | (c & 63)), k = 3;
        else
            b[0] = (unsigned char)(0xF0 | c >> 18), b[1] = (unsigned char)(0x80 | (c >> 12 & 63)),
            b[2] = (unsigned char)(0x80 | (c >> 6 & 63)), b[3] = (unsigned char)(0x80 | (c & 63)), k = 4;
        if (out)
            for (size_t j = 0; j < k; j++) out[bytes + j] = (char)b[j];
        bytes += k;
    }
    return bytes;
}

/* One character; returns the bytes used, or 0 when the bytes are malformed. */
static size_t decode_one(const unsigned char *s, size_t n, uint32_t *code) {
    unsigned lead = s[0];
    size_t more;
    uint32_t c, least;
    if (lead < 0x80) return *code = lead, 1;
    if (lead >= 0xC2 && lead <= 0xDF) more = 1, c = lead & 0x1F, least = 0x80;
    else if (lead >= 0xE0 && lead <= 0xEF) more = 2, c = lead & 0x0F, least = 0x800;
    else if (lead >= 0xF0 && lead <= 0xF4) more = 3, c = lead & 0x07, least = 0x10000;
    else return 0;
    if (n < more + 1) return 0;
    for (size_t i = 1; i <= more; i++) {
        if ((s[i] & 0xC0) != 0x80) return 0;
        c = c << 6 | (s[i] & 0x3F);
    }
    if (c < least || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return 0;
    *code = c;
    return more + 1;
}

size_t utf8_to_utf16(const char *text, size_t n, wchar_t *out, bool lossy) {
    const unsigned char *s = (const unsigned char *)text;
    size_t count = 0;
    for (size_t i = 0; i < n;) {
        uint32_t c;
        size_t used = decode_one(s + i, n - i, &c);
        if (!used) {
            if (!lossy) return SIZE_MAX;
            c = 0xFFFD, used = 1;
        }
        i += used;
        if (c >= 0x10000) {
            if (out) out[count] = (wchar_t)(0xD800 + ((c - 0x10000) >> 10)), out[count + 1] = (wchar_t)(0xDC00 + ((c - 0x10000) & 0x3FF));
            count += 2;
        } else {
            if (out) out[count] = (wchar_t)c;
            count++;
        }
    }
    return count;
}

size_t utf8_partial_tail(const unsigned char *s, size_t n) {
    for (size_t k = 1; k <= 3 && k <= n; k++) {
        unsigned b = s[n - k];
        if ((b & 0xC0) == 0x80) continue; /* a continuation byte: keep looking for its lead */
        size_t need = b >= 0xF0 && b <= 0xF4 ? 4 : b >= 0xE0 && b <= 0xEF ? 3 : b >= 0xC2 && b <= 0xDF ? 2 : 0;
        return need > k ? k : 0;
    }
    return 0;
}

bool parse_u64(const wchar_t *s, uint64_t *value) {
    unsigned base = 10;
    if (s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')) base = 16, s += 2;
    uint64_t v = 0;
    size_t digits = 0;
    for (; *s; s++, digits++) {
        unsigned d;
        wchar_t c = *s;
        if (c >= L'0' && c <= L'9') d = (unsigned)(c - L'0');
        else if (base == 16 && c >= L'a' && c <= L'f') d = (unsigned)(c - L'a' + 10);
        else if (base == 16 && c >= L'A' && c <= L'F') d = (unsigned)(c - L'A' + 10);
        else break;
        if (v > (UINT64_MAX - d) / base) return false;
        v = v * base + d;
    }
    if (!digits) return false;
    unsigned shift = 0;
    switch (wascii_lower(*s)) {
    case 0: break;
    case L'k': shift = 10; break;
    case L'm': shift = 20; break;
    case L'g': shift = 30; break;
    case L't': shift = 40; break;
    default: return false;
    }
    if (*s && s[1]) return false;
    if (shift && v > UINT64_MAX >> shift) return false;
    *value = v << shift;
    return true;
}

size_t format_u64(uint64_t value, char *out) {
    char tmp[20];
    size_t n = 0;
    do tmp[n++] = (char)('0' + value % 10), value /= 10;
    while (value);
    for (size_t i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = 0;
    return n;
}

size_t format_size(uint64_t bytes, bool human, char *out) {
    char digits[24];
    size_t n = 0;
    if (!human) {
        size_t d = format_u64(bytes, digits);
        for (size_t i = 0; i < d; i++) {
            if (i && (d - i) % 3 == 0) out[n++] = ',';
            out[n++] = digits[i];
        }
        out[n] = 0;
        return n;
    }
    static const char *const unit[] = {"KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
    if (bytes < 1024) {
        n = format_u64(bytes, out);
        out[n++] = ' ', out[n++] = 'B', out[n] = 0;
        return n;
    }
    int u = 0;
    uint64_t scale = 1024;
    while (u < 5 && bytes / scale >= 1024) u++, scale <<= 10;
    n = format_u64(bytes / scale, out);
    out[n++] = '.', out[n++] = (char)('0' + (bytes % scale) * 10 / scale), out[n++] = ' ';
    for (const char *p = unit[u]; *p; p++) out[n++] = *p;
    out[n] = 0;
    return n;
}

int wname_fold_compare(const wchar_t *a, const wchar_t *b) {
    for (;; a++, b++) {
        wchar_t p = wascii_lower(*a), q = wascii_lower(*b);
        if (p != q) return (uint16_t)p < (uint16_t)q ? -1 : 1;
        if (!*a) return 0;
    }
}

int wname_compare(const wchar_t *a, const wchar_t *b) {
    int c = wname_fold_compare(a, b);
    if (c) return c;
    for (;; a++, b++) {
        if (*a != *b) return (uint16_t)*a < (uint16_t)*b ? -1 : 1;
        if (!*a) return 0;
    }
}
