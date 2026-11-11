// NT4DNC.EXE: holt CNC-Programme per HTTP vom Server und schreibt sie auf die Maschine.
//
// Laeuft auf Windows NT 4.0. Nur Win32, GDI und Winsock, keine C++-Standardbibliothek und keine
// Gleitkommazahlen: Die statische UCRT nutzt fuer printf/strtod SSE, und das gibt es auf NT 4 nicht.
//
// Ablauf beim Schreiben eines Programms:
//   1. Programm per HTTP in eine Zwischendatei im Zielordner laden, dabei CRC32 rechnen.
//   2. Laenge und CRC32 mit den Angaben des Servers vergleichen, Zwischendatei noch einmal von der
//      Platte lesen und pruefen.
//   3. Gibt es das Programm schon und ist es anders, die alte Fassung in den Sicherungsordner kopieren.
//   4. Zwischendatei auf den echten Namen umbenennen. Eine halbe Datei liegt nie unter dem echten Namen.
//   5. Ergebnis lokal und auf dem Server protokollieren.
//
// Aufruf:
//   NT4DNC.EXE               Fenster mit Programmliste
//   NT4DNC.EXE -list         Liste auf stdout (Test, Skripte)
//   NT4DNC.EXE -get NAME     ein Programm schreiben, Ergebnis auf stdout, Exitcode 0 = ok
// Einstellungen stehen in NT4DNC.INI neben der EXE (Vorlage: NT4DNC.example.ini).

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>

// ---------------------------------------------------------------------------------------------
// Farben (Stykker tokens.css, Dark-Theme, wie nt4-window)

#define CLR_BG     RGB(6, 9, 22)
#define CLR_CARD   RGB(17, 26, 51)
#define CLR_BORDER RGB(33, 76, 102)
#define CLR_INK    RGB(232, 238, 255)
#define CLR_MUTED  RGB(130, 146, 184)
#define CLR_ACC    RGB(79, 227, 255)
#define CLR_GOOD   RGB(108, 255, 176)
#define CLR_BAD    RGB(255, 92, 122)

#define ID_LIST    201
#define ID_REFRESH 101
#define ID_WRITE   102
#define ID_QUIT    103

#define MAX_PROGRAMS 2000
#define NAME_CAP     65
#define LIST_LIMIT   (1024 * 1024)

static const char kVersion[] = "0.1";

// ---------------------------------------------------------------------------------------------
// Kleine Helfer ohne CRT

static void StrCopy(char* dst, int cap, const char* src)
{
    lstrcpynA(dst, src, cap);
}

static void StrCat(char* dst, int cap, const char* src)
{
    int n = lstrlenA(dst);
    if (n < cap - 1)
        lstrcpynA(dst + n, src, cap - n);
}

static char LowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// Vergleicht den Anfang von text mit prefix, ohne Gross/Klein zu beachten.
static bool StartsWithI(const char* text, const char* prefix)
{
    for (; *prefix; ++text, ++prefix)
        if (LowerAscii(*text) != LowerAscii(*prefix))
            return false;
    return true;
}

static const char* SkipSpaces(const char* p)
{
    while (*p == ' ' || *p == '\t')
        ++p;
    return p;
}

static DWORD ParseDec(const char* p, bool* ok)
{
    DWORD v = 0;
    bool any = false;
    for (; *p >= '0' && *p <= '9'; ++p)
    {
        v = v * 10 + (DWORD)(*p - '0');
        any = true;
    }
    if (ok)
        *ok = any;
    return v;
}

static DWORD ParseHex(const char* p, bool* ok)
{
    DWORD v = 0;
    int n = 0;
    for (;; ++p, ++n)
    {
        char c = LowerAscii(*p);
        if (c >= '0' && c <= '9')
            v = (v << 4) | (DWORD)(c - '0');
        else if (c >= 'a' && c <= 'f')
            v = (v << 4) | (DWORD)(c - 'a' + 10);
        else
            break;
    }
    if (ok)
        *ok = n > 0 && n <= 8;
    return v;
}

// Programmnamen wie auf dem Server: Buchstaben, Ziffern, _ . -, kein Pfadteil, hoechstens 64 Zeichen.
// Doppelte Pruefung, damit ein falscher Server nie ausserhalb des Zielordners schreiben kann.
static bool IsValidProgramName(const char* name)
{
    int n = lstrlenA(name);
    if (n < 1 || n > 64)
        return false;
    if (name[0] == '.' || name[0] == '-')
        return false;
    for (int i = 0; i < n; ++i)
    {
        char c = name[i];
        bool okChar = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '.' || c == '-';
        if (!okChar)
            return false;
        if (c == '.' && name[i + 1] == '.')
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// CRC32 (IEEE, wie Dnc/Crc32.cs auf dem Server)

static DWORD g_crcTable[256];

static void CrcInit()
{
    for (DWORD i = 0; i < 256; ++i)
    {
        DWORD c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        g_crcTable[i] = c;
    }
}

static DWORD CrcUpdate(DWORD crc, const BYTE* p, DWORD n)
{
    while (n--)
        crc = g_crcTable[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return crc;
}

// CRC32 einer Datei. Liefert false, wenn sie nicht lesbar ist.
static bool FileCrc(const char* path, DWORD* crcOut, DWORD* sizeOut)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    BYTE  buf[8192];
    DWORD crc = 0xFFFFFFFFu, total = 0, got = 0;
    bool  ok = true;
    for (;;)
    {
        if (!ReadFile(h, buf, sizeof(buf), &got, NULL))
        {
            ok = false;
            break;
        }
        if (got == 0)
            break;
        crc = CrcUpdate(crc, buf, got);
        total += got;
    }
    CloseHandle(h);
    if (ok)
    {
        *crcOut = crc ^ 0xFFFFFFFFu;
        if (sizeOut)
            *sizeOut = total;
    }
    return ok;
}

// ---------------------------------------------------------------------------------------------
// Einstellungen aus NT4DNC.INI

struct Config
{
    char ini[MAX_PATH];
    char host[128];
    int  port;
    char machine[40];
    char token[128];
    char target[MAX_PATH];
    char backup[MAX_PATH];
    char logFile[MAX_PATH];
};

static Config g_cfg;

static void TrimSlash(char* path)
{
    int n = lstrlenA(path);
    while (n > 3 && (path[n - 1] == '\\' || path[n - 1] == '/'))
        path[--n] = 0;
}

static void LoadConfig()
{
    GetModuleFileNameA(NULL, g_cfg.ini, MAX_PATH);
    char* slash = NULL;
    for (char* p = g_cfg.ini; *p; ++p)
        if (*p == '\\')
            slash = p;
    if (slash)
        slash[1] = 0;
    StrCat(g_cfg.ini, MAX_PATH, "NT4DNC.INI");

    GetPrivateProfileStringA("Server", "Host", "127.0.0.1", g_cfg.host, sizeof(g_cfg.host), g_cfg.ini);
    g_cfg.port = (int)GetPrivateProfileIntA("Server", "Port", 5080, g_cfg.ini);
    GetPrivateProfileStringA("Maschine", "Name", "", g_cfg.machine, sizeof(g_cfg.machine), g_cfg.ini);
    GetPrivateProfileStringA("Maschine", "Schluessel", "", g_cfg.token, sizeof(g_cfg.token), g_cfg.ini);
    GetPrivateProfileStringA("Ablage", "Ziel", "C:\\DNC\\EIN", g_cfg.target, sizeof(g_cfg.target), g_cfg.ini);
    GetPrivateProfileStringA("Ablage", "Sicherung", "C:\\DNC\\ALT", g_cfg.backup, sizeof(g_cfg.backup), g_cfg.ini);
    GetPrivateProfileStringA("Ablage", "Protokoll", "C:\\DNC\\NT4DNC.LOG", g_cfg.logFile, sizeof(g_cfg.logFile), g_cfg.ini);
    TrimSlash(g_cfg.target);
    TrimSlash(g_cfg.backup);
}

// ---------------------------------------------------------------------------------------------
// Dateisystem

// Legt einen Ordner samt fehlender Elternordner an.
static bool EnsureDir(const char* dir)
{
    char path[MAX_PATH];
    StrCopy(path, MAX_PATH, dir);
    int n = lstrlenA(path);
    for (int i = 3; i <= n; ++i)
    {
        if (path[i] == '\\' || path[i] == 0)
        {
            char keep = path[i];
            path[i] = 0;
            if (!CreateDirectoryA(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
            {
                DWORD attr = GetFileAttributesA(path);
                if (attr == 0xFFFFFFFF || !(attr & FILE_ATTRIBUTE_DIRECTORY))
                    return false;
            }
            path[i] = keep;
        }
    }
    return true;
}

static void JoinPath(char* out, const char* dir, const char* name)
{
    StrCopy(out, MAX_PATH, dir);
    StrCat(out, MAX_PATH, "\\");
    StrCat(out, MAX_PATH, name);
}

// ---------------------------------------------------------------------------------------------
// Protokoll

static void LogLocal(const char* text)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[600];
    wsprintfA(line, "%04d-%02d-%02d %02d:%02d:%02d %s\r\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
              st.wSecond, text);

    char dir[MAX_PATH];
    StrCopy(dir, MAX_PATH, g_cfg.logFile);
    for (int i = lstrlenA(dir) - 1; i > 2; --i)
        if (dir[i] == '\\')
        {
            dir[i] = 0;
            EnsureDir(dir);
            break;
        }

    HANDLE h = CreateFileA(g_cfg.logFile, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    SetFilePointer(h, 0, NULL, FILE_END);
    DWORD written;
    WriteFile(h, line, (DWORD)lstrlenA(line), &written, NULL);
    CloseHandle(h);
}

// ---------------------------------------------------------------------------------------------
// HTTP/1.0 ueber Winsock. Der Antwortkoerper geht stueckweise an eine Senke.

typedef bool (*BodySink)(void* ctx, const char* data, DWORD len);

struct HttpResult
{
    int   status;
    bool  hasLength;
    DWORD contentLength;
    bool  hasCrc;
    DWORD crc;
    DWORD bodyBytes;
    char  error[256];
};

static bool SendAll(SOCKET s, const char* data, int len)
{
    while (len > 0)
    {
        int n = send(s, data, len, 0);
        if (n <= 0)
            return false;
        data += n;
        len -= n;
    }
    return true;
}

static void ParseHeaders(char* head, HttpResult* r)
{
    // Statuszeile: HTTP/1.x NNN Text
    char* p = head;
    while (*p && *p != ' ')
        ++p;
    r->status = (int)ParseDec(SkipSpaces(p), NULL);

    for (char* line = head; *line;)
    {
        char* end = line;
        while (*end && *end != '\r' && *end != '\n')
            ++end;
        char keep = *end;
        *end = 0;
        bool ok = false;
        if (StartsWithI(line, "content-length:"))
        {
            DWORD v = ParseDec(SkipSpaces(line + 15), &ok);
            if (ok)
            {
                r->hasLength = true;
                r->contentLength = v;
            }
        }
        else if (StartsWithI(line, "x-crc32:"))
        {
            DWORD v = ParseHex(SkipSpaces(line + 8), &ok);
            if (ok)
            {
                r->hasCrc = true;
                r->crc = v;
            }
        }
        *end = keep;
        line = end;
        while (*line == '\r' || *line == '\n')
            ++line;
    }
}

static bool HttpRequest(const char* method, const char* path, const char* body, DWORD bodyLen, BodySink sink,
                        void* ctx, HttpResult* r)
{
    ZeroMemory(r, sizeof(*r));

    sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)g_cfg.port);
    addr.sin_addr.s_addr = inet_addr(g_cfg.host);
    if (addr.sin_addr.s_addr == INADDR_NONE)
    {
        hostent* he = gethostbyname(g_cfg.host);
        if (!he)
        {
            wsprintfA(r->error, "Server %s nicht gefunden", g_cfg.host);
            return false;
        }
        CopyMemory(&addr.sin_addr, he->h_addr_list[0], sizeof(addr.sin_addr));
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
    {
        StrCopy(r->error, sizeof(r->error), "socket() fehlgeschlagen");
        return false;
    }
    int timeoutMs = 15000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));

    if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0)
    {
        wsprintfA(r->error, "Keine Verbindung zu %s:%d", g_cfg.host, g_cfg.port);
        closesocket(s);
        return false;
    }

    char req[1024];
    wsprintfA(req,
              "%s %s HTTP/1.0\r\n"
              "Host: %s:%d\r\n"
              "User-Agent: StykkerNT4Dnc/%s\r\n"
              "X-Nt4-Machine: %s\r\n"
              "X-Nt4-Token: %s\r\n"
              "Connection: close\r\n",
              method, path, g_cfg.host, g_cfg.port, kVersion, g_cfg.machine, g_cfg.token);
    if (body)
    {
        char extra[96];
        wsprintfA(extra, "Content-Type: text/plain\r\nContent-Length: %lu\r\n", bodyLen);
        StrCat(req, sizeof(req), extra);
    }
    StrCat(req, sizeof(req), "\r\n");

    if (!SendAll(s, req, lstrlenA(req)) || (body && bodyLen && !SendAll(s, body, (int)bodyLen)))
    {
        StrCopy(r->error, sizeof(r->error), "Senden an den Server fehlgeschlagen");
        closesocket(s);
        return false;
    }

    // Kopf sammeln bis zur Leerzeile, Rest ist Koerper.
    char head[8192];
    int  headLen = 0;
    bool inBody = false;
    bool ok = true;
    char buf[8192];
    for (;;)
    {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n == SOCKET_ERROR)
        {
            StrCopy(r->error, sizeof(r->error), "Server antwortet nicht (Zeitueberschreitung)");
            ok = false;
            break;
        }
        if (n == 0)
            break;
        const char* data = buf;
        if (!inBody)
        {
            int take = n;
            if (headLen + take > (int)sizeof(head) - 1)
                take = (int)sizeof(head) - 1 - headLen;
            CopyMemory(head + headLen, buf, take);
            int oldLen = headLen;
            headLen += take;
            head[headLen] = 0;
            int split = -1;
            for (int i = (oldLen > 3 ? oldLen - 3 : 0); i + 3 < headLen; ++i)
                if (head[i] == '\r' && head[i + 1] == '\n' && head[i + 2] == '\r' && head[i + 3] == '\n')
                {
                    split = i;
                    break;
                }
            if (split < 0)
            {
                if (headLen >= (int)sizeof(head) - 1)
                {
                    StrCopy(r->error, sizeof(r->error), "Antwortkopf zu gross");
                    ok = false;
                    break;
                }
                continue;
            }
            head[split] = 0;
            ParseHeaders(head, r);
            inBody = true;
            // Was nach der Leerzeile in diesem Paket steht, gehoert zum Koerper.
            int bodyStart = split + 4 - oldLen;
            data = buf + bodyStart;
            n -= bodyStart;
        }
        if (n > 0)
        {
            r->bodyBytes += (DWORD)n;
            if (sink && !sink(ctx, data, (DWORD)n))
            {
                StrCopy(r->error, sizeof(r->error), "Schreiben fehlgeschlagen");
                ok = false;
                break;
            }
        }
    }
    closesocket(s);
    if (ok && !inBody)
    {
        StrCopy(r->error, sizeof(r->error), "Ungueltige Antwort vom Server");
        ok = false;
    }
    return ok;
}

// Senke: Speicher (fuer Liste und kurze Antworten)
struct MemBuf
{
    char* data;
    DWORD len;
    DWORD cap;
};

static bool MemSink(void* ctx, const char* data, DWORD len)
{
    MemBuf* m = (MemBuf*)ctx;
    if (m->len + len + 1 > LIST_LIMIT)
        return false;
    if (m->len + len + 1 > m->cap)
    {
        DWORD cap = m->cap ? m->cap : 16384;
        while (cap < m->len + len + 1)
            cap *= 2;
        char* p = m->data ? (char*)HeapReAlloc(GetProcessHeap(), 0, m->data, cap)
                          : (char*)HeapAlloc(GetProcessHeap(), 0, cap);
        if (!p)
            return false;
        m->data = p;
        m->cap = cap;
    }
    CopyMemory(m->data + m->len, data, len);
    m->len += len;
    m->data[m->len] = 0;
    return true;
}

static void MemFree(MemBuf* m)
{
    if (m->data)
        HeapFree(GetProcessHeap(), 0, m->data);
    ZeroMemory(m, sizeof(*m));
}

// Senke: Datei, rechnet nebenbei die CRC32
struct FileSinkCtx
{
    HANDLE h;
    DWORD  crc;
};

static bool FileSink(void* ctx, const char* data, DWORD len)
{
    FileSinkCtx* f = (FileSinkCtx*)ctx;
    DWORD written = 0;
    if (!WriteFile(f->h, data, len, &written, NULL) || written != len)
        return false;
    f->crc = CrcUpdate(f->crc, (const BYTE*)data, len);
    return true;
}

// Fehlertext des Servers (erste Zeile des Koerpers) in msg uebernehmen.
static void ServerError(const HttpResult* r, const MemBuf* m, char* msg, int cap)
{
    char first[200] = "";
    if (m && m->data)
    {
        int i = 0;
        while (m->data[i] && m->data[i] != '\r' && m->data[i] != '\n' && i < (int)sizeof(first) - 1)
        {
            first[i] = m->data[i];
            ++i;
        }
        first[i] = 0;
    }
    wsprintfA(msg, "Server meldet %d %s", r->status, first);
    (void)cap;
}

static void LogServer(const char* text)
{
    HttpResult r;
    MemBuf     m = {};
    HttpRequest("POST", "/nt4/api/dnc/log", text, (DWORD)lstrlenA(text), MemSink, &m, &r);
    MemFree(&m);
}

// ---------------------------------------------------------------------------------------------
// Programmliste

enum LocalState
{
    LocalMissing,
    LocalSame,
    LocalDiffers,
};

struct Program
{
    char  name[NAME_CAP];
    DWORD size;
    char  stamp[15]; // YYYYMMDDhhmmss
    DWORD crc;
    int   local;
};

static Program g_programs[MAX_PROGRAMS];
static int     g_count;

static void CompareLocal(Program* p)
{
    char  path[MAX_PATH];
    DWORD crc = 0;
    JoinPath(path, g_cfg.target, p->name);
    if (!FileCrc(path, &crc, NULL))
        p->local = LocalMissing;
    else
        p->local = crc == p->crc ? LocalSame : LocalDiffers;
}

// Laedt die Liste vom Server. msg bekommt Fehler oder eine kurze Zusammenfassung.
static bool LoadList(char* msg, int cap)
{
    g_count = 0;
    HttpResult r;
    MemBuf     m = {};
    if (!HttpRequest("GET", "/nt4/api/dnc/list", NULL, 0, MemSink, &m, &r))
    {
        StrCopy(msg, cap, r.error);
        MemFree(&m);
        return false;
    }
    if (r.status != 200)
    {
        ServerError(&r, &m, msg, cap);
        MemFree(&m);
        return false;
    }

    int skipped = 0;
    for (char* line = m.data; line && *line && g_count < MAX_PROGRAMS;)
    {
        char* end = line;
        while (*end && *end != '\r' && *end != '\n')
            ++end;
        char keep = *end;
        *end = 0;

        // NAME;SIZE;STAMP;CRC
        char* f[4] = {};
        int   nf = 0;
        f[nf++] = line;
        for (char* p = line; *p && nf < 4; ++p)
            if (*p == ';')
            {
                *p = 0;
                f[nf++] = p + 1;
            }
        if (nf == 4 && IsValidProgramName(f[0]) && lstrlenA(f[2]) == 14)
        {
            bool okSize, okCrc;
            Program* p = &g_programs[g_count];
            StrCopy(p->name, NAME_CAP, f[0]);
            p->size = ParseDec(f[1], &okSize);
            StrCopy(p->stamp, sizeof(p->stamp), f[2]);
            p->crc = ParseHex(f[3], &okCrc);
            if (okSize && okCrc)
            {
                CompareLocal(p);
                ++g_count;
            }
            else
                ++skipped;
        }
        else if (*line)
            ++skipped;

        *end = keep;
        line = end;
        while (*line == '\r' || *line == '\n')
            ++line;
    }
    MemFree(&m);

    if (skipped)
        wsprintfA(msg, "%d Programme, %d ungueltige Zeilen uebersprungen", g_count, skipped);
    else
        wsprintfA(msg, "%d Programme auf dem Server fuer %s", g_count, g_cfg.machine);
    return true;
}

static const char* LocalText(int state)
{
    switch (state)
    {
    case LocalSame: return "gleich";
    case LocalDiffers: return "ANDERS";
    default: return "fehlt";
    }
}

// "YYYYMMDDhhmmss" -> "DD.MM.YYYY hh:mm"
static void FormatStamp(const char* s, char* out)
{
    wsprintfA(out, "%c%c.%c%c.%c%c%c%c %c%c:%c%c", s[6], s[7], s[4], s[5], s[0], s[1], s[2], s[3], s[8], s[9],
              s[10], s[11]);
}

// ---------------------------------------------------------------------------------------------
// Ein Programm auf die Maschine schreiben

static bool WriteProgram(const char* name, char* msg, int cap)
{
    char logText[400];
    if (!IsValidProgramName(name))
    {
        wsprintfA(msg, "Ungueltiger Programmname: %s", name);
        return false;
    }
    if (!EnsureDir(g_cfg.target))
    {
        wsprintfA(msg, "Zielordner %s fehlt und laesst sich nicht anlegen", g_cfg.target);
        return false;
    }

    char target[MAX_PATH], temp[MAX_PATH];
    JoinPath(target, g_cfg.target, name);
    JoinPath(temp, g_cfg.target, "~NT4DNC.TMP");

    DWORD attr = GetFileAttributesA(target);
    bool  exists = attr != 0xFFFFFFFF;
    if (exists && (attr & FILE_ATTRIBUTE_READONLY))
    {
        wsprintfA(msg, "%s ist schreibgeschuetzt, nichts geaendert", name);
        return false;
    }

    // 1. Laden in die Zwischendatei
    FileSinkCtx f;
    f.h = CreateFileA(temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f.h == INVALID_HANDLE_VALUE)
    {
        wsprintfA(msg, "Zwischendatei %s laesst sich nicht anlegen (Fehler %lu)", temp, GetLastError());
        return false;
    }
    f.crc = 0xFFFFFFFFu;

    char path[200];
    wsprintfA(path, "/nt4/api/dnc/file/%s", name);
    HttpResult r;
    bool got = HttpRequest("GET", path, NULL, 0, FileSink, &f, &r);
    FlushFileBuffers(f.h);
    CloseHandle(f.h);
    DWORD crc = f.crc ^ 0xFFFFFFFFu;

    // 2. Pruefen
    if (!got)
    {
        DeleteFileA(temp);
        StrCopy(msg, cap, r.error);
        return false;
    }
    if (r.status != 200)
    {
        DeleteFileA(temp);
        wsprintfA(msg, "Server meldet %d fuer %s", r.status, name);
        return false;
    }
    if (!r.hasCrc || (r.hasLength && r.contentLength != r.bodyBytes) || crc != r.crc)
    {
        DeleteFileA(temp);
        wsprintfA(msg, "%s unvollstaendig oder fehlerhaft uebertragen (%lu Byte), nichts geaendert", name, r.bodyBytes);
        wsprintfA(logText, "FEHLER %s Pruefsumme/Laenge falsch", name);
        LogLocal(logText);
        LogServer(logText);
        return false;
    }
    DWORD diskCrc = 0, diskSize = 0;
    if (!FileCrc(temp, &diskCrc, &diskSize) || diskCrc != crc || diskSize != r.bodyBytes)
    {
        DeleteFileA(temp);
        wsprintfA(msg, "%s: Zwischendatei auf der Platte fehlerhaft, nichts geaendert", name);
        return false;
    }

    // 3. Alte Fassung sichern, falls anders
    char backupNote[MAX_PATH + 40] = "";
    char backupPath[MAX_PATH] = "";
    if (exists)
    {
        DWORD oldCrc = 0;
        if (!FileCrc(target, &oldCrc, NULL))
        {
            DWORD err = GetLastError();
            DeleteFileA(temp);
            if (err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION || err == ERROR_ACCESS_DENIED)
                wsprintfA(msg, "%s ist gerade in Benutzung (Fehler %lu), nichts geaendert", name, err);
            else
                wsprintfA(msg, "%s auf der Maschine nicht lesbar (Fehler %lu), nichts geaendert", name, err);
            return false;
        }
        if (oldCrc == crc)
        {
            DeleteFileA(temp);
            wsprintfA(msg, "%s ist schon aktuell, nichts geaendert", name);
            wsprintfA(logText, "GLEICH %s CRC=%08lX", name, crc);
            LogLocal(logText);
            LogServer(logText);
            return true;
        }
        if (!EnsureDir(g_cfg.backup))
        {
            DeleteFileA(temp);
            wsprintfA(msg, "Sicherungsordner %s fehlt, nichts geaendert", g_cfg.backup);
            return false;
        }
        // Sicherungsname mit Zeitstempel; gibt es ihn schon (zweimal in derselben Sekunde), Zaehler anhaengen.
        SYSTEMTIME st;
        GetLocalTime(&st);
        char  backupName[MAX_PATH];
        DWORD err = ERROR_FILE_EXISTS;
        for (int n = 1; n <= 99 && err == ERROR_FILE_EXISTS; ++n)
        {
            if (n == 1)
                wsprintfA(backupName, "%04d%02d%02d-%02d%02d%02d_%s", st.wYear, st.wMonth, st.wDay, st.wHour,
                          st.wMinute, st.wSecond, name);
            else
                wsprintfA(backupName, "%04d%02d%02d-%02d%02d%02d-%d_%s", st.wYear, st.wMonth, st.wDay, st.wHour,
                          st.wMinute, st.wSecond, n, name);
            JoinPath(backupPath, g_cfg.backup, backupName);
            err = CopyFileA(target, backupPath, TRUE) ? 0 : GetLastError();
        }
        if (err != 0)
        {
            DeleteFileA(temp);
            backupPath[0] = 0;
            wsprintfA(msg, "Sicherung von %s fehlgeschlagen (Fehler %lu), nichts geaendert", name, err);
            return false;
        }
        wsprintfA(backupNote, ", alte Fassung in %s", backupName);
    }

    // 4. Umbenennen auf den echten Namen
    if (!MoveFileExA(temp, target, MOVEFILE_REPLACE_EXISTING))
    {
        DWORD err = GetLastError();
        DeleteFileA(temp);
        if (backupPath[0])
            DeleteFileA(backupPath); // Original ist unveraendert, die Kopie wird nicht gebraucht
        if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED)
            wsprintfA(msg, "%s ist gerade in Benutzung (Fehler %lu), nichts geaendert", name, err);
        else
            wsprintfA(msg, "%s konnte nicht geschrieben werden (Fehler %lu)", name, err);
        wsprintfA(logText, "FEHLER %s Umbenennen Fehler %lu", name, err);
        LogLocal(logText);
        LogServer(logText);
        return false;
    }

    // 5. Protokoll
    wsprintfA(msg, "%s geschrieben (%lu Byte)%s", name, r.bodyBytes, backupNote);
    wsprintfA(logText, "%s %s %lu Byte CRC=%08lX%s", exists ? "ERSETZT" : "NEU", name, r.bodyBytes, crc, backupNote);
    LogLocal(logText);
    LogServer(logText);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Kommandozeile

static void Out(const char* text)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == NULL || h == INVALID_HANDLE_VALUE)
        return;
    DWORD written;
    WriteFile(h, text, (DWORD)lstrlenA(text), &written, NULL);
}

// Zerlegt die Kommandozeile in hoechstens 8 Teile (ohne Programmnamen).
static int SplitArgs(char* cmd, char** argv, int max)
{
    char* p = cmd;
    // Programmname ueberspringen
    if (*p == '"')
    {
        ++p;
        while (*p && *p != '"')
            ++p;
        if (*p)
            ++p;
    }
    else
        while (*p && *p != ' ')
            ++p;

    int n = 0;
    while (*p && n < max)
    {
        while (*p == ' ')
            ++p;
        if (!*p)
            break;
        if (*p == '"')
        {
            argv[n++] = ++p;
            while (*p && *p != '"')
                ++p;
        }
        else
        {
            argv[n++] = p;
            while (*p && *p != ' ')
                ++p;
        }
        if (*p)
            *p++ = 0;
    }
    return n;
}

static int RunCommandLine(int argc, char** argv)
{
    char msg[600], line[300];
    if (lstrcmpiA(argv[0], "-list") == 0)
    {
        if (!LoadList(msg, sizeof(msg)))
        {
            Out("FEHLER ");
            Out(msg);
            Out("\r\n");
            return 1;
        }
        for (int i = 0; i < g_count; ++i)
        {
            const Program* p = &g_programs[i];
            char when[20];
            FormatStamp(p->stamp, when);
            wsprintfA(line, "%-24s %10lu  %s  %08lX  %s\r\n", p->name, p->size, when, p->crc, LocalText(p->local));
            Out(line);
        }
        Out(msg);
        Out("\r\n");
        return 0;
    }
    if (lstrcmpiA(argv[0], "-get") == 0 && argc >= 2)
    {
        bool ok = WriteProgram(argv[1], msg, sizeof(msg));
        Out(ok ? "OK " : "FEHLER ");
        Out(msg);
        Out("\r\n");
        return ok ? 0 : 1;
    }
    Out("Aufruf: NT4DNC.EXE [-list | -get NAME]\r\n");
    return 2;
}

// ---------------------------------------------------------------------------------------------
// Fenster

static HWND   g_wnd, g_list, g_btnRefresh, g_btnWrite, g_btnQuit;
static HFONT  g_fontCaps, g_fontTitle, g_fontBody, g_fontMono;
static HBRUSH g_brBg, g_brCard;
static char   g_status[600] = "Noch keine Liste geladen";
static bool   g_statusBad;

static void SetStatus(const char* text, bool bad)
{
    StrCopy(g_status, sizeof(g_status), text);
    g_statusBad = bad;
    RECT rc;
    GetClientRect(g_wnd, &rc);
    rc.top = rc.bottom - 34;
    InvalidateRect(g_wnd, &rc, TRUE);
    UpdateWindow(g_wnd);
}

static void FillList()
{
    SendMessageA(g_list, WM_SETREDRAW, FALSE, 0);
    SendMessageA(g_list, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_count; ++i)
    {
        const Program* p = &g_programs[i];
        char when[20], line[160];
        FormatStamp(p->stamp, when);
        wsprintfA(line, "%-24s %10lu  %s  %s", p->name, p->size, when, LocalText(p->local));
        SendMessageA(g_list, LB_ADDSTRING, 0, (LPARAM)line);
    }
    SendMessageA(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, NULL, TRUE);
    if (g_count > 0)
        SendMessageA(g_list, LB_SETCURSEL, 0, 0);
}

static void DoRefresh()
{
    char msg[600];
    HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    SetStatus("Lade Liste vom Server ...", false);
    int keep = (int)SendMessageA(g_list, LB_GETCURSEL, 0, 0);
    bool ok = LoadList(msg, sizeof(msg));
    FillList();
    if (keep >= 0 && keep < g_count)
        SendMessageA(g_list, LB_SETCURSEL, keep, 0);
    SetCursor(old);
    SetStatus(msg, !ok);
}

static void DoWrite()
{
    int sel = (int)SendMessageA(g_list, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= g_count)
    {
        SetStatus("Erst ein Programm in der Liste waehlen", true);
        return;
    }
    Program* p = &g_programs[sel];
    char question[700];
    wsprintfA(question,
              "%s auf die Maschine schreiben?\r\n\r\nZiel: %s\\%s\r\nGroesse: %lu Byte\r\nAuf der Maschine: %s%s",
              p->name, g_cfg.target, p->name, p->size, LocalText(p->local),
              p->local == LocalDiffers ? "\r\n\r\nDie vorhandene Fassung wird vorher gesichert." : "");
    if (MessageBoxA(g_wnd, question, "Stykker DNC", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;

    char msg[600];
    HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    SetStatus("Uebertrage ...", false);
    bool ok = WriteProgram(p->name, msg, sizeof(msg));
    CompareLocal(p);
    char saved[600];
    StrCopy(saved, sizeof(saved), msg);
    FillList();
    SendMessageA(g_list, LB_SETCURSEL, sel, 0);
    SetCursor(old);
    SetStatus(saved, !ok);
    if (!ok)
        MessageBoxA(g_wnd, saved, "Stykker DNC", MB_OK | MB_ICONERROR);
}

static void Layout(int w, int h)
{
    MoveWindow(g_list, 16, 92, w - 32, h - 92 - 92, TRUE);
    int y = h - 80;
    MoveWindow(g_btnRefresh, 16, y, 150, 30, TRUE);
    MoveWindow(g_btnWrite, 176, y, 230, 30, TRUE);
    MoveWindow(g_btnQuit, w - 16 - 110, y, 110, 30, TRUE);
}

static void Paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, g_brBg);
    SetBkMode(hdc, TRANSPARENT);

    RECT card = {16, 12, rc.right - 16, 80};
    FillRect(hdc, &card, g_brCard);
    HPEN pen = CreatePen(PS_SOLID, 1, CLR_BORDER);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, card.left, card.top, card.right, card.bottom);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    SelectObject(hdc, g_fontCaps);
    SetTextColor(hdc, CLR_ACC);
    TextOutA(hdc, 30, 20, "STYKKER DNC", 11);

    char line[300];
    SelectObject(hdc, g_fontTitle);
    SetTextColor(hdc, CLR_INK);
    wsprintfA(line, "Maschine %s", g_cfg.machine[0] ? g_cfg.machine : "(nicht eingestellt)");
    TextOutA(hdc, 30, 36, line, lstrlenA(line));

    SelectObject(hdc, g_fontBody);
    SetTextColor(hdc, CLR_MUTED);
    wsprintfA(line, "Server %s:%d   Ziel %s", g_cfg.host, g_cfg.port, g_cfg.target);
    RECT info = {30, 58, card.right - 12, 76};
    DrawTextA(hdc, line, -1, &info, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_PATH_ELLIPSIS);

    SetTextColor(hdc, g_statusBad ? CLR_BAD : CLR_GOOD);
    TextOutA(hdc, 16, rc.bottom - 30, g_status, lstrlenA(g_status));

    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HINSTANCE inst = ((CREATESTRUCTA*)lp)->hInstance;
        g_list = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                 0, 0, 10, 10, hwnd, (HMENU)ID_LIST, inst, NULL);
        g_btnRefresh = CreateWindowA("BUTTON", "Liste laden (F5)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                     0, 0, 10, 10, hwnd, (HMENU)ID_REFRESH, inst, NULL);
        g_btnWrite = CreateWindowA("BUTTON", "Auf Maschine schreiben (Enter)",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10, hwnd,
                                   (HMENU)ID_WRITE, inst, NULL);
        g_btnQuit = CreateWindowA("BUTTON", "Beenden", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10,
                                  10, hwnd, (HMENU)ID_QUIT, inst, NULL);
        SendMessageA(g_list, WM_SETFONT, (WPARAM)g_fontMono, TRUE);
        SendMessageA(g_btnRefresh, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
        SendMessageA(g_btnWrite, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
        SendMessageA(g_btnQuit, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
        return 0;
    }
    case WM_SIZE:
        Layout(LOWORD(lp), HIWORD(lp));
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint(hwnd);
        return 0;
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)wp, CLR_INK);
        SetBkColor((HDC)wp, CLR_CARD);
        return (LRESULT)g_brCard;
    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case ID_REFRESH: DoRefresh(); break;
        case ID_WRITE: DoWrite(); break;
        case ID_QUIT: DestroyWindow(hwnd); break;
        case ID_LIST:
            if (HIWORD(wp) == LBN_DBLCLK)
                DoWrite();
            break;
        }
        return 0;
    case WM_APP:
        DoRefresh();
        SetFocus(g_list);
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lp)->ptMinTrackSize.x = 560;
        ((MINMAXINFO*)lp)->ptMinTrackSize.y = 360;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static HFONT MakeFont(int height, int weight, const char* face, DWORD pitch)
{
    return CreateFontA(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, pitch | FF_DONTCARE, face);
}

static int RunWindow(HINSTANCE inst, int show)
{
    g_fontCaps = MakeFont(11, FW_BOLD, "Arial", VARIABLE_PITCH);
    g_fontTitle = MakeFont(18, FW_BOLD, "Arial", VARIABLE_PITCH);
    g_fontBody = MakeFont(13, FW_NORMAL, "Arial", VARIABLE_PITCH);
    g_fontMono = MakeFont(14, FW_NORMAL, "Courier New", FIXED_PITCH);
    g_brBg = CreateSolidBrush(CLR_BG);
    g_brCard = CreateSolidBrush(CLR_CARD);

    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = g_brBg;
    wc.lpszClassName = "StykkerNt4Dnc";
    RegisterClassA(&wc);

    char title[80];
    wsprintfA(title, "Stykker DNC %s", kVersion);
    g_wnd = CreateWindowA("StykkerNt4Dnc", title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 720, 520, NULL,
                          NULL, inst, NULL);
    if (!g_wnd)
        return 1;
    ShowWindow(g_wnd, show);
    UpdateWindow(g_wnd);
    PostMessageA(g_wnd, WM_APP, 0, 0);

    MSG m;
    while (GetMessageA(&m, NULL, 0, 0) > 0)
    {
        if (m.message == WM_KEYDOWN && m.wParam == VK_F5)
        {
            DoRefresh();
            continue;
        }
        if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN && m.hwnd == g_list)
        {
            DoWrite();
            continue;
        }
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE)
        {
            DestroyWindow(g_wnd);
            continue;
        }
        if (IsDialogMessageA(g_wnd, &m))
            continue;
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show)
{
    CrcInit();
    LoadConfig();

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(1, 1), &wsa) != 0)
    {
        MessageBoxA(NULL, "Winsock konnte nicht gestartet werden.", "Stykker DNC", MB_ICONERROR);
        return 1;
    }

    char  cmd[1024];
    char* argv[8];
    StrCopy(cmd, sizeof(cmd), GetCommandLineA());
    int argc = SplitArgs(cmd, argv, 8);

    int rc = argc > 0 ? RunCommandLine(argc, argv) : RunWindow(inst, show);
    WSACleanup();
    return rc;
}
