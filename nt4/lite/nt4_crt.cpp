// Replacements for UCRT functions that contain SSE instructions.
//
// The static UCRT from current Windows SDKs is built for SSE2. Its printf/strtod
// floating point code and its rounding/fenv helpers use SSE registers without a
// CPU check. NT 4 never enables SSE (CR4.OSFXSR), so those instructions fault with
// "illegal instruction". The functions below take precedence over the UCRT objects
// at link time. This file is compiled with -march=pentium-mmx and -fno-builtin, so
// it only uses x87 floating point.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdarg.h>
#include <stdint.h>

namespace
{
    // ------------------------------------------------------------ output sink

    template <typename C> struct Sink
    {
        C*     buf;
        size_t cap;  // writable characters, without the terminator
        size_t len = 0;

        void put(C c)
        {
            if (buf && len < cap)
                buf[len] = c;
            ++len;
        }
        void repeat(C c, int n)
        {
            while (n-- > 0)
                put(c);
        }
    };

    // ------------------------------------------------------------ floating point

    double Pow10(int e)
    {
        double r = 1.0, b = 10.0;
        unsigned n = (unsigned)(e < 0 ? -e : e);
        while (n)
        {
            if (n & 1)
                r *= b;
            b *= b;
            n >>= 1;
        }
        return e < 0 ? 1.0 / r : r;
    }

    bool IsNan(double v)
    {
        return v != v;
    }

    bool IsInf(double v)
    {
        return v == v && (v - v) != (v - v);
    }

    // Digits of v (v >= 0) as decimal: fills digits[] with up to 'count' significant digits,
    // returns the decimal exponent of the first digit.
    int Decompose(double v, char* digits, int count)
    {
        if (v == 0.0)
        {
            for (int i = 0; i < count; ++i)
                digits[i] = '0';
            return 0;
        }
        int e = 0;
        while (v >= 10.0)
        {
            if (v >= 1e16) { v /= 1e16; e += 16; }
            else { v /= 10.0; ++e; }
        }
        while (v < 1.0)
        {
            if (v < 1e-16) { v *= 1e16; e -= 16; }
            else { v *= 10.0; --e; }
        }
        for (int i = 0; i < count; ++i)
        {
            int d = (int)v;
            if (d > 9)
                d = 9;
            digits[i] = (char)('0' + d);
            v = (v - d) * 10.0;
        }
        // round the last digit
        if (v >= 5.0)
        {
            int i = count - 1;
            while (i >= 0 && digits[i] == '9')
                digits[i--] = '0';
            if (i >= 0)
                digits[i]++;
            else
            {
                digits[0] = '1';
                for (int k = 1; k < count; ++k)
                    digits[k] = '0';
                ++e;
            }
        }
        return e;
    }

    struct FloatSpec
    {
        char conv;     // f, e, g (lower case)
        bool upper;
        bool alt;
        int  prec;
    };

    // Formats |v| (finite, >= 0) without sign into out (big enough: 400 chars).
    int FormatFloat(double v, FloatSpec s, char* out)
    {
        int  n = 0;
        char digits[400];

        char conv = s.conv;
        int  prec = s.prec < 0 ? 6 : s.prec;
        bool strip = false;
        if (conv == 'g')
        {
            int p = prec == 0 ? 1 : prec;
            if (p > 40)
                p = 40;
            int x = Decompose(v, digits, p);
            if (p > x && x >= -4)
            {
                conv = 'f';
                prec = p - 1 - x;
            }
            else
            {
                conv = 'e';
                prec = p - 1;
            }
            strip = !s.alt;
        }
        if (prec > 60)
            prec = 60;

        if (conv == 'f')
        {
            int x     = v == 0.0 ? 0 : Decompose(v, digits, 1);
            int total = x + 1 + prec; // significant digits needed
            if (total > 300)
                total = 300;
            if (total <= 0)
            {
                // value rounds to 0 or to one unit in the last place
                double unit = Pow10(-prec);
                bool   up   = v >= unit / 2;
                out[n++]    = '0';
                if (prec > 0 || s.alt)
                    out[n++] = '.';
                for (int i = 0; i < prec; ++i)
                    out[n++] = (up && i == prec - 1) ? '1' : '0';
            }
            else
            {
                int x2 = Decompose(v, digits, total);
                if (x2 != x) // rounding carried into a new digit
                {
                    ++total;
                    x2 = Decompose(v, digits, total);
                }
                int intDigits = x2 + 1;
                if (intDigits <= 0)
                {
                    out[n++] = '0';
                }
                else
                {
                    for (int i = 0; i < intDigits; ++i)
                        out[n++] = i < total ? digits[i] : '0';
                }
                if (prec > 0 || s.alt)
                    out[n++] = '.';
                for (int i = 0; i < prec; ++i)
                {
                    int idx = intDigits + i;
                    out[n++] = (idx >= 0 && idx < total) ? digits[idx] : '0';
                }
            }
        }
        else // 'e'
        {
            int x    = Decompose(v, digits, prec + 1);
            out[n++] = digits[0];
            if (prec > 0 || s.alt)
                out[n++] = '.';
            for (int i = 1; i <= prec; ++i)
                out[n++] = digits[i];
            if (v == 0.0)
                x = 0;
            if (strip)
            {
                while (n > 0 && out[n - 1] == '0')
                    --n;
                if (n > 0 && out[n - 1] == '.')
                    --n;
                strip = false;
            }
            out[n++] = s.upper ? 'E' : 'e';
            out[n++] = x < 0 ? '-' : '+';
            int ax   = x < 0 ? -x : x;
            if (ax >= 100)
                out[n++] = (char)('0' + ax / 100);
            out[n++] = (char)('0' + (ax / 10) % 10);
            out[n++] = (char)('0' + ax % 10);
        }

        if (strip)
        {
            bool hasDot = false;
            for (int i = 0; i < n; ++i)
                if (out[i] == '.')
                    hasDot = true;
            if (hasDot)
            {
                while (n > 0 && out[n - 1] == '0')
                    --n;
                if (n > 0 && out[n - 1] == '.')
                    --n;
            }
        }
        out[n] = 0;
        return n;
    }

    // ------------------------------------------------------------ printf core

    template <typename C, typename F>
    int Format(Sink<C>& sink, const F* fmt, va_list ap, bool wideFormat, bool legacyWide)
    {
        while (*fmt)
        {
            if (*fmt != '%')
            {
                sink.put((C)*fmt++);
                continue;
            }
            ++fmt;
            if (*fmt == '%')
            {
                sink.put((C)'%');
                ++fmt;
                continue;
            }

            bool left = false, plus = false, space = false, alt = false, zero = false;
            for (;; ++fmt)
            {
                if (*fmt == '-') left = true;
                else if (*fmt == '+') plus = true;
                else if (*fmt == ' ') space = true;
                else if (*fmt == '#') alt = true;
                else if (*fmt == '0') zero = true;
                else break;
            }

            int width = 0;
            if (*fmt == '*')
            {
                width = va_arg(ap, int);
                if (width < 0) { left = true; width = -width; }
                ++fmt;
            }
            else
                while (*fmt >= '0' && *fmt <= '9')
                    width = width * 10 + (*fmt++ - '0');

            int prec = -1;
            if (*fmt == '.')
            {
                ++fmt;
                prec = 0;
                if (*fmt == '*')
                {
                    prec = va_arg(ap, int);
                    ++fmt;
                }
                else
                    while (*fmt >= '0' && *fmt <= '9')
                        prec = prec * 10 + (*fmt++ - '0');
            }

            // length: 0 int, 1 long, 2 long long / I64, 3 size_t, 4 short, 5 char
            int  len   = 0;
            bool lflag = false, hflag = false;
            for (;;)
            {
                if (*fmt == 'l')
                {
                    if (fmt[1] == 'l') { len = 2; fmt += 2; }
                    else { len = 1; lflag = true; ++fmt; }
                }
                else if (*fmt == 'h')
                {
                    if (fmt[1] == 'h') { len = 5; fmt += 2; }
                    else { len = 4; hflag = true; ++fmt; }
                }
                else if (*fmt == 'z' || *fmt == 't' || *fmt == 'j')
                {
                    len = *fmt == 'j' ? 2 : 3;
                    ++fmt;
                }
                else if (*fmt == 'L' || *fmt == 'w')
                {
                    if (*fmt == 'w') lflag = true;
                    ++fmt;
                }
                else if (*fmt == 'I')
                {
                    if (fmt[1] == '6' && fmt[2] == '4') { len = 2; fmt += 3; }
                    else if (fmt[1] == '3' && fmt[2] == '2') { len = 0; fmt += 3; }
                    else { len = 3; ++fmt; }
                }
                else
                    break;
            }

            F conv = *fmt;
            if (!conv)
                break;
            ++fmt;

            char  tmp[420];
            int   tn     = 0;
            char  sign   = 0;
            const char*    str  = nullptr;
            const wchar_t* wstr = nullptr;
            int   slen   = 0;
            const char* prefix = "";

            switch (conv)
            {
            case 'd':
            case 'i':
            case 'u':
            case 'x':
            case 'X':
            case 'o':
            case 'p':
            {
                unsigned long long u;
                bool               neg = false;
                if (conv == 'p')
                {
                    u    = (uintptr_t)va_arg(ap, void*);
                    prec = (int)(sizeof(void*) * 2);
                }
                else if (conv == 'd' || conv == 'i')
                {
                    long long v;
                    if (len == 2) v = va_arg(ap, long long);
                    else if (len == 1) v = va_arg(ap, long);
                    else if (len == 3) v = (long long)va_arg(ap, intptr_t);
                    else v = va_arg(ap, int);
                    if (len == 4) v = (short)v;
                    if (len == 5) v = (signed char)v;
                    neg = v < 0;
                    u   = neg ? 0ULL - (unsigned long long)v : (unsigned long long)v;
                }
                else
                {
                    if (len == 2) u = va_arg(ap, unsigned long long);
                    else if (len == 1) u = va_arg(ap, unsigned long);
                    else if (len == 3) u = (unsigned long long)va_arg(ap, size_t);
                    else u = va_arg(ap, unsigned int);
                    if (len == 4) u = (unsigned short)u;
                    if (len == 5) u = (unsigned char)u;
                }
                unsigned base = (conv == 'x' || conv == 'X' || conv == 'p') ? 16 : conv == 'o' ? 8 : 10;
                const char* dig = (conv == 'X' || conv == 'p') ? "0123456789ABCDEF" : "0123456789abcdef";
                char rev[32];
                int  rn = 0;
                while (u)
                {
                    rev[rn++] = dig[u % base];
                    u /= base;
                }
                int p = prec < 0 ? 1 : prec;
                while (rn < p)
                    rev[rn++] = '0';
                if (alt && conv == 'o' && rev[rn - 1] != '0')
                    rev[rn++] = '0';
                while (rn)
                    tmp[tn++] = rev[--rn];
                if (neg) sign = '-';
                else if ((conv == 'd' || conv == 'i') && plus) sign = '+';
                else if ((conv == 'd' || conv == 'i') && space) sign = ' ';
                if (alt && tn && (conv == 'x' || conv == 'X'))
                    prefix = conv == 'x' ? "0x" : "0X";
                if (prec >= 0)
                    zero = false;
                str  = tmp;
                slen = tn;
                break;
            }
            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
            case 'a':
            case 'A':
            {
                double v = va_arg(ap, double);
                if (v < 0 || (v == 0 && 1 / v < 0))
                {
                    sign = '-';
                    v    = -v;
                }
                else if (plus) sign = '+';
                else if (space) sign = ' ';
                bool upper = conv == 'F' || conv == 'E' || conv == 'G' || conv == 'A';
                if (IsNan(v) || IsInf(v))
                {
                    const char* t = IsNan(v) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
                    tn   = (int)strlen(t);
                    memcpy(tmp, t, tn);
                    zero = false;
                }
                else
                {
                    char c = (char)(conv | 0x20);
                    if (c == 'a')
                        c = 'e';
                    FloatSpec fs = {c, upper, alt, prec};
                    tn           = FormatFloat(v, fs, tmp);
                }
                str  = tmp;
                slen = tn;
                break;
            }
            case 'c':
            case 'C':
            {
                bool wideChar = conv == 'C' ? !wideFormat || !legacyWide : lflag || (wideFormat && legacyWide && !hflag);
                if (wideChar)
                    tmp[0] = (char)(wchar_t)va_arg(ap, int);
                else
                    tmp[0] = (char)va_arg(ap, int);
                str  = tmp;
                slen = 1;
                break;
            }
            case 's':
            case 'S':
            {
                bool wideStr = conv == 'S' ? !(wideFormat && legacyWide) : lflag || (wideFormat && legacyWide && !hflag);
                if (wideStr)
                {
                    wstr = va_arg(ap, const wchar_t*);
                    if (!wstr)
                        wstr = L"(null)";
                    slen = 0;
                    while (wstr[slen] && (prec < 0 || slen < prec))
                        ++slen;
                }
                else
                {
                    str = va_arg(ap, const char*);
                    if (!str)
                        str = "(null)";
                    slen = 0;
                    while (str[slen] && (prec < 0 || slen < prec))
                        ++slen;
                }
                zero = false;
                break;
            }
            case 'n':
                (void)va_arg(ap, void*);
                continue;
            default:
                sink.put((C)'%');
                sink.put((C)conv);
                continue;
            }

            int plen = (int)strlen(prefix);
            int body = slen + (sign ? 1 : 0) + plen;
            int pad  = width > body ? width - body : 0;
            if (!left && !zero)
                sink.repeat((C)' ', pad);
            if (sign)
                sink.put((C)sign);
            for (int i = 0; i < plen; ++i)
                sink.put((C)prefix[i]);
            if (!left && zero)
                sink.repeat((C)'0', pad);
            if (wstr)
                for (int i = 0; i < slen; ++i)
                    sink.put((C)wstr[i]);
            else
                for (int i = 0; i < slen; ++i)
                    sink.put((C)(unsigned char)str[i]);
            if (left)
                sink.repeat((C)' ', pad);
        }
        return (int)sink.len;
    }

    template <typename C, typename F>
    int FormatToBuffer(unsigned long long options, C* buffer, size_t count, const F* format, va_list args,
                       bool wideFormat)
    {
        bool legacyWide = (options & _CRT_INTERNAL_PRINTF_LEGACY_WIDE_SPECIFIERS) != 0;
        bool standard   = (options & _CRT_INTERNAL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR) != 0;

        if (!buffer || count == 0)
        {
            Sink<C> probe = {nullptr, 0};
            return Format(probe, format, args, wideFormat, legacyWide);
        }

        // Legacy _vsnprintf may fill the whole buffer without a terminator.
        Sink<C> sink = {buffer, standard ? count - 1 : count};
        int     n    = Format(sink, format, args, wideFormat, legacyWide);

        if ((size_t)n < count)
        {
            buffer[n] = 0;
            return n;
        }
        if (standard)
        {
            buffer[count - 1] = 0;
            return n;
        }
        return (size_t)n == count ? n : -1;
    }

    // ------------------------------------------------------------ number parsing

    template <typename C> double ParseDouble(const C* s, C** end)
    {
        const C* p = s;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v')
            ++p;
        bool neg = false;
        if (*p == '+' || *p == '-')
            neg = *p++ == '-';

        double   v      = 0.0;
        int      exp10  = 0;
        bool     digits = false;
        int      sig    = 0;
        while (*p >= '0' && *p <= '9')
        {
            if (sig < 18) { v = v * 10.0 + (*p - '0'); if (v != 0) ++sig; }
            else ++exp10;
            ++p;
            digits = true;
        }
        if (*p == '.')
        {
            ++p;
            while (*p >= '0' && *p <= '9')
            {
                if (sig < 18) { v = v * 10.0 + (*p - '0'); --exp10; if (v != 0) ++sig; }
                ++p;
                digits = true;
            }
        }
        if (!digits)
        {
            if (end)
                *end = (C*)s;
            return 0.0;
        }
        if (*p == 'e' || *p == 'E')
        {
            const C* q  = p + 1;
            bool     en = false;
            if (*q == '+' || *q == '-')
                en = *q++ == '-';
            if (*q >= '0' && *q <= '9')
            {
                int e = 0;
                while (*q >= '0' && *q <= '9')
                {
                    if (e < 10000)
                        e = e * 10 + (*q - '0');
                    ++q;
                }
                exp10 += en ? -e : e;
                p = q;
            }
        }
        if (exp10)
            v = exp10 < 0 ? v / Pow10(-exp10) : v * Pow10(exp10);
        if (end)
            *end = (C*)p;
        return neg ? -v : v;
    }

    double FloorD(double x)
    {
        if (x != x || x >= 4503599627370496.0 || x <= -4503599627370496.0)
            return x;
        double t = (double)(long long)x;
        return t > x ? t - 1.0 : t;
    }

    double CeilD(double x)
    {
        if (x != x || x >= 4503599627370496.0 || x <= -4503599627370496.0)
            return x;
        double t = (double)(long long)x;
        return t < x ? t + 1.0 : t;
    }

    double RoundD(double x)
    {
        return x < 0 ? -FloorD(-x + 0.5) : FloorD(x + 0.5);
    }

    // pow() for CSS number parsing and colors. Integral exponents are exact by repeated squaring;
    // the rest goes through series for ln and exp, which is accurate enough for layout.
    double PowInt(double x, long long n)
    {
        double r = 1.0, b = x;
        unsigned long long u = n < 0 ? 0ULL - (unsigned long long)n : (unsigned long long)n;
        while (u)
        {
            if (u & 1)
                r *= b;
            b *= b;
            u >>= 1;
        }
        return n < 0 ? 1.0 / r : r;
    }

    double LnD(double x)
    {
        const double ln2 = 0.69314718055994530942;
        int          k   = 0;
        while (x >= 2.0) { x *= 0.5; ++k; }
        while (x < 1.0) { x *= 2.0; --k; }
        double s = (x - 1.0) / (x + 1.0), s2 = s * s, term = s, sum = 0.0;
        for (int i = 1; i < 60; i += 2)
        {
            sum  += term / i;
            term *= s2;
        }
        return 2.0 * sum + k * ln2;
    }

    double ExpD(double z)
    {
        const double ln2 = 0.69314718055994530942;
        if (z > 709.0)
            return 1.0 / 0.0;
        if (z < -745.0)
            return 0.0;
        double n    = (double)(long long)(z / ln2 + (z < 0 ? -0.5 : 0.5));
        double r    = z - n * ln2;
        double term = 1.0, sum = 1.0;
        for (int i = 1; i < 30; ++i)
        {
            term *= r / i;
            sum  += term;
        }
        return sum * PowInt(2.0, (long long)n);
    }

    double PowD(double x, double y)
    {
        if (y == 0.0)
            return 1.0;
        if (x != x || y != y)
            return x + y;
        bool integral = y > -9.2e18 && y < 9.2e18 && (double)(long long)y == y;
        if (integral)
            return PowInt(x, (long long)y);
        if (x < 0.0)
            return 0.0 / 0.0;
        if (x == 0.0)
            return y > 0 ? 0.0 : 1.0 / 0.0;
        return ExpD(y * LnD(x));
    }

    double RintD(double x)
    {
        double f = FloorD(x);
        double d = x - f;
        if (d > 0.5 || (d == 0.5 && FloorD(f / 2) * 2 != f))
            return f + 1.0;
        return f;
    }
}

extern "C"
{
    // ------------------------------------------------------------ printf family

    int __cdecl __stdio_common_vsprintf(unsigned __int64 options, char* buffer, size_t count, const char* format,
                                        _locale_t, va_list args)
    {
        return FormatToBuffer<char, char>(options, buffer, count, format, args, false);
    }

    int __cdecl __stdio_common_vsprintf_s(unsigned __int64 options, char* buffer, size_t count, const char* format,
                                          _locale_t, va_list args)
    {
        int n = FormatToBuffer<char, char>(options | _CRT_INTERNAL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR, buffer, count,
                                           format, args, false);
        if (buffer && count && (size_t)n >= count)
        {
            buffer[0] = 0;
            return -1;
        }
        return n;
    }

    int __cdecl __stdio_common_vsnprintf_s(unsigned __int64 options, char* buffer, size_t count, size_t maxCount,
                                           const char* format, _locale_t, va_list args)
    {
        size_t limit = maxCount == (size_t)-1 || maxCount + 1 > count ? count : maxCount + 1;
        int n = FormatToBuffer<char, char>(options | _CRT_INTERNAL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR, buffer, limit,
                                           format, args, false);
        return (size_t)n >= limit ? -1 : n;
    }

    int __cdecl __stdio_common_vswprintf(unsigned __int64 options, wchar_t* buffer, size_t count,
                                         const wchar_t* format, _locale_t, va_list args)
    {
        return FormatToBuffer<wchar_t, wchar_t>(options, buffer, count, format, args, true);
    }

    int __cdecl __stdio_common_vfprintf(unsigned __int64 options, FILE* stream, const char* format, _locale_t,
                                        va_list args)
    {
        va_list copy;
        va_copy(copy, args);
        int n = FormatToBuffer<char, char>(options, nullptr, 0, format, copy, false);
        va_end(copy);
        if (n <= 0)
            return n;
        char* text = (char*)malloc((size_t)n + 1);
        if (!text)
            return -1;
        FormatToBuffer<char, char>(options | _CRT_INTERNAL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR, text, (size_t)n + 1,
                                   format, args, false);
        fwrite(text, 1, (size_t)n, stream);
        free(text);
        return n;
    }

    int __cdecl __stdio_common_vfwprintf(unsigned __int64 options, FILE* stream, const wchar_t* format, _locale_t,
                                         va_list args)
    {
        va_list copy;
        va_copy(copy, args);
        int n = FormatToBuffer<char, wchar_t>(options, nullptr, 0, format, copy, true);
        va_end(copy);
        if (n <= 0)
            return n;
        char* text = (char*)malloc((size_t)n + 1);
        if (!text)
            return -1;
        FormatToBuffer<char, wchar_t>(options | _CRT_INTERNAL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR, text,
                                      (size_t)n + 1, format, args, true);
        fwrite(text, 1, (size_t)n, stream);
        free(text);
        return n;
    }

    // ------------------------------------------------------------ number parsing

    double __cdecl strtod(const char* s, char** end)
    {
        return ParseDouble(s, end);
    }

    float __cdecl strtof(const char* s, char** end)
    {
        return (float)ParseDouble(s, end);
    }

    long double __cdecl strtold(const char* s, char** end)
    {
        return ParseDouble(s, end);
    }

    double __cdecl _strtod_l(const char* s, char** end, _locale_t)
    {
        return ParseDouble(s, end);
    }

    float __cdecl _strtof_l(const char* s, char** end, _locale_t)
    {
        return (float)ParseDouble(s, end);
    }

    long double __cdecl _strtold_l(const char* s, char** end, _locale_t)
    {
        return ParseDouble(s, end);
    }

    double __cdecl wcstod(const wchar_t* s, wchar_t** end)
    {
        return ParseDouble(s, end);
    }

    float __cdecl wcstof(const wchar_t* s, wchar_t** end)
    {
        return (float)ParseDouble(s, end);
    }

    long double __cdecl wcstold(const wchar_t* s, wchar_t** end)
    {
        return ParseDouble(s, end);
    }

    double __cdecl atof(const char* s)
    {
        return ParseDouble<char>(s, nullptr);
    }

    // ------------------------------------------------------------ rounding

    double __cdecl pow(double x, double y) { return PowD(x, y); }
    double __cdecl floor(double x) { return FloorD(x); }
    double __cdecl ceil(double x) { return CeilD(x); }
    double __cdecl round(double x) { return RoundD(x); }
    float __cdecl roundf(float x) { return (float)RoundD(x); }
    double __cdecl trunc(double x) { return x < 0 ? CeilD(x) : FloorD(x); }
    float __cdecl truncf(float x) { return (float)(x < 0 ? CeilD(x) : FloorD(x)); }
    double __cdecl rint(double x) { return RintD(x); }
    float __cdecl rintf(float x) { return (float)RintD(x); }
    double __cdecl nearbyint(double x) { return RintD(x); }
    float __cdecl nearbyintf(float x) { return (float)RintD(x); }
    long __cdecl lround(double x) { return (long)RoundD(x); }
    long __cdecl lroundf(float x) { return (long)RoundD(x); }
    long __cdecl lrint(double x) { return (long)RintD(x); }
    long __cdecl lrintf(float x) { return (long)RintD(x); }
    long long __cdecl llround(double x) { return (long long)RoundD(x); }
    long long __cdecl llrint(double x) { return (long long)RintD(x); }

    // ------------------------------------------------------------ fenv (x87 only)

    int __cdecl fegetround(void)
    {
        unsigned short cw;
        __asm__ __volatile__("fnstcw %0" : "=m"(cw));
        switch ((cw >> 10) & 3)
        {
        case 0: return 0x000;  // FE_TONEAREST
        case 1: return 0x100;  // FE_DOWNWARD
        case 2: return 0x200;  // FE_UPWARD
        default: return 0x300; // FE_TOWARDZERO
        }
    }

    int __cdecl fesetround(int mode)
    {
        unsigned short cw;
        __asm__ __volatile__("fnstcw %0" : "=m"(cw));
        unsigned short rc = mode == 0x100 ? 1 : mode == 0x200 ? 2 : mode == 0x300 ? 3 : 0;
        cw = (unsigned short)((cw & ~0x0C00) | (rc << 10));
        __asm__ __volatile__("fldcw %0" : : "m"(cw));
        return 0;
    }
}
