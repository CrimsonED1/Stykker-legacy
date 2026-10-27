// Stykker Lite: a small HTML view for Windows NT 4.0.
// Loads a page over plain HTTP (Winsock), renders it with litehtml onto GDI,
// follows links, and reloads when the page asks for it (meta refresh / Refresh header).
// No JavaScript: the server drives the page.

#include <winsock2.h>
#include <windows.h>

#include <litehtml.h>

// nanosvg uses strtoll, whose UCRT version contains SSE (see nt4_crt.cpp).
extern "C" long long __cdecl nt4_strtoll(const char* s, char** end, int base);
#define strtoll nt4_strtoll
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg.h>
#include <nanosvgrast.h>

#include <string>
#include <vector>
#include <set>
#include <map>
#include <memory>

namespace
{
    const wchar_t* kClassName  = L"StykkerNT4Lite";
    const char*    kDefaultUrl = "http://192.168.178.146:5080/lite";
    const UINT     WM_APP_NAVIGATE = WM_APP + 1;
    const UINT     WM_APP_REDRAW   = WM_APP + 2;
    const UINT_PTR kRefreshTimer   = 1;

    // ---------------------------------------------------------------- strings

    std::wstring Widen(const std::string& s)
    {
        if (s.empty())
            return std::wstring();
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
        return w;
    }

    std::string Narrow(const std::wstring& w)
    {
        if (w.empty())
            return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
        return s;
    }

    std::string Lower(std::string s)
    {
        for (auto& c : s)
            if (c >= 'A' && c <= 'Z')
                c = (char)(c - 'A' + 'a');
        return s;
    }

    std::string Trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n\"'");
        size_t b = s.find_last_not_of(" \t\r\n\"'");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    }

    double NowMs()
    {
        static LARGE_INTEGER freq = {};
        if (!freq.QuadPart)
            QueryPerformanceFrequency(&freq);
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
    }

    // ---------------------------------------------------------------- URLs

    struct Url
    {
        std::string host;
        int         port = 80;
        std::string path = "/";
    };

    bool ParseUrl(const std::string& url, Url& out)
    {
        const std::string prefix = "http://";
        if (Lower(url.substr(0, prefix.size())) != prefix)
            return false;
        std::string rest  = url.substr(prefix.size());
        size_t      slash = rest.find('/');
        std::string hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
        out.path = slash == std::string::npos ? "/" : rest.substr(slash);
        size_t colon = hostport.find(':');
        out.host = colon == std::string::npos ? hostport : hostport.substr(0, colon);
        out.port = colon == std::string::npos ? 80 : atoi(hostport.c_str() + colon + 1);
        return !out.host.empty() && out.port > 0;
    }

    std::string ResolveUrl(const std::string& base, const std::string& href)
    {
        if (href.empty())
            return base;
        if (Lower(href.substr(0, 7)) == "http://" || Lower(href.substr(0, 8)) == "https://")
            return href;

        Url b;
        if (!ParseUrl(base, b))
            return href;
        std::string origin = "http://" + b.host + (b.port == 80 ? "" : ":" + std::to_string(b.port));

        if (href[0] == '/')
            return origin + href;

        std::string path = b.path.substr(0, b.path.find_first_of("?#"));
        if (href[0] == '?')
            return origin + path + href;
        return origin + path.substr(0, path.rfind('/') + 1) + href;
    }

    // ---------------------------------------------------------------- HTTP

    struct HttpResponse
    {
        int         status = 0;
        std::string headers;
        std::string body;
        std::string finalUrl;
    };

    std::string HeaderValue(const std::string& headers, const std::string& name)
    {
        std::string lowerHeaders = Lower(headers);
        std::string key          = "\r\n" + Lower(name) + ":";
        size_t      pos          = lowerHeaders.find(key);
        if (pos == std::string::npos)
            return std::string();
        pos += key.size();
        size_t end = headers.find("\r\n", pos);
        return Trim(headers.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
    }

    // Split of the last request: TCP connect, first response byte after the request, end of data.
    struct HttpTiming
    {
        double connect = 0, firstByte = 0, rest = 0;
    } g_lastHttp;

    bool HttpGetOnce(const std::string& url, HttpResponse& resp, std::string& error)
    {
        double t0 = NowMs();
        Url u;
        if (!ParseUrl(url, u))
        {
            error = "Nur http:// wird unterstuetzt: " + url;
            return false;
        }

        sockaddr_in addr = {};
        addr.sin_family  = AF_INET;
        addr.sin_port    = htons((u_short)u.port);
        addr.sin_addr.s_addr = inet_addr(u.host.c_str());
        if (addr.sin_addr.s_addr == INADDR_NONE)
        {
            hostent* he = gethostbyname(u.host.c_str());
            if (!he)
            {
                error = "Host nicht gefunden: " + u.host;
                return false;
            }
            memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(addr.sin_addr));
        }

        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
        {
            error = "socket() fehlgeschlagen";
            return false;
        }
        int timeoutMs = 5000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));

        if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0)
        {
            error = "Keine Verbindung zu " + u.host + ":" + std::to_string(u.port);
            closesocket(s);
            return false;
        }

        std::string request = "GET " + u.path + " HTTP/1.0\r\n"
                              "Host: " + u.host + ":" + std::to_string(u.port) + "\r\n"
                              "User-Agent: StykkerNT4Lite/0.1\r\n"
                              "Connection: close\r\n\r\n";
        double t1 = NowMs();
        send(s, request.c_str(), (int)request.size(), 0);

        std::string raw;
        char        buf[4096];
        double      t2 = 0;
        for (;;)
        {
            int n = recv(s, buf, sizeof(buf), 0);
            if (n <= 0)
                break;
            if (raw.empty())
                t2 = NowMs();
            raw.append(buf, n);
        }
        closesocket(s);
        double t3 = NowMs();
        g_lastHttp.connect   = t1 - t0;
        g_lastHttp.firstByte = t2 ? t2 - t1 : t3 - t1;
        g_lastHttp.rest      = t2 ? t3 - t2 : 0;

        size_t split = raw.find("\r\n\r\n");
        if (split == std::string::npos)
        {
            error = "Ungueltige Antwort von " + url;
            return false;
        }
        resp.headers = "\r\n" + raw.substr(0, split) + "\r\n";
        resp.body    = raw.substr(split + 4);
        size_t sp    = raw.find(' ');
        resp.status  = sp == std::string::npos ? 0 : atoi(raw.c_str() + sp + 1);
        resp.finalUrl = url;
        return true;
    }

    bool HttpGet(std::string url, HttpResponse& resp, std::string& error)
    {
        for (int hop = 0; hop < 5; ++hop)
        {
            if (!HttpGetOnce(url, resp, error))
                return false;
            if (resp.status < 300 || resp.status >= 400)
                return true;
            std::string location = HeaderValue(resp.headers, "Location");
            if (location.empty())
                return true;
            url = ResolveUrl(url, location);
        }
        error = "Zu viele Weiterleitungen";
        return false;
    }

    // ---------------------------------------------------------------- app state

    HWND                    g_hwnd = nullptr;
    litehtml::document::ptr g_doc;
    std::string             g_url;
    std::string             g_baseUrl;
    std::string             g_pendingUrl;
    std::string             g_refreshUrl;
    int                     g_scrollY = 0;
    int                     g_clientW = 0;
    int                     g_clientH = 0;

    // Timing of the last load cycle, shown in the title bar.
    std::string g_title;
    double      g_msFetch  = 0;
    double      g_msImages = 0;
    double      g_msLayout = 0;
    double      g_msParse  = 0;  // HTML + CSS into a litehtml document
    double      g_msDraw   = 0;  // litehtml into the back buffer
    double      g_msBlit   = 0;  // changed areas to the screen
    int         g_blitKpx  = 0;  // copied to the screen, in 1000 pixels
    double      g_msCycle  = 0;
    LONGLONG    g_lastNavigate = 0;
    HttpTiming  g_pageHttp;

    void UpdateTitle()
    {
        // Short form, so it fits the title bar (ms): load (connect/answer), images, layout, draw, blit, cycle.
        char stats[200];
        snprintf(stats, sizeof(stats), "L %d (%d/%d) B %d P %d Lay %d Z %d Blit %d/%dk Takt %d - ", (int)g_msFetch,
                 (int)g_pageHttp.connect, (int)g_pageHttp.firstByte, (int)g_msImages, (int)g_msParse, (int)g_msLayout,
                 (int)g_msDraw, (int)g_msBlit, g_blitKpx, (int)g_msCycle);
        SetWindowTextW(g_hwnd, Widen(stats + g_title).c_str());
    }

    // ---------------------------------------------------------------- GDI container

    COLORREF ToColorRef(const litehtml::web_color& c)
    {
        return RGB(c.red, c.green, c.blue);
    }

    void FillBox(HDC hdc, int x, int y, int w, int h, const litehtml::web_color& c)
    {
        if (w <= 0 || h <= 0 || c.alpha == 0)
            return;
        RECT   rc = {x, y, x + w, y + h};
        HBRUSH br = CreateSolidBrush(ToColorRef(c));
        FillRect(hdc, &rc, br);
        DeleteObject(br);
    }

    litehtml::web_color Mix(const litehtml::web_color& a, const litehtml::web_color& b, float t)
    {
        return litehtml::web_color((litehtml::byte)(a.red + (b.red - a.red) * t),
                                   (litehtml::byte)(a.green + (b.green - a.green) * t),
                                   (litehtml::byte)(a.blue + (b.blue - a.blue) * t));
    }

    class GdiContainer : public litehtml::document_container
    {
      public:
        GdiContainer()
        {
            m_hdc = CreateCompatibleDC(nullptr);
            EnumFontFamiliesW(m_hdc, nullptr, (FONTENUMPROCW)&GdiContainer::OnFontFamily, (LPARAM)this);
        }

        ~GdiContainer()
        {
            for (auto& f : m_fonts)
                DeleteObject(f.second.font);
            m_images.clear();
            nsvgDeleteRasterizer(m_rast);
            DeleteDC(m_hdc);
        }

        litehtml::uint_ptr create_font(const litehtml::font_description& descr, const litehtml::document*,
                                       litehtml::font_metrics* fm) override
        {
            LOGFONTW lf = {};
            lstrcpynW(lf.lfFaceName, PickFace(descr.family).c_str(), LF_FACESIZE);
            lf.lfHeight    = -(LONG)(descr.size.value() + 0.5f);
            lf.lfWeight    = descr.weight;
            lf.lfItalic    = descr.style == litehtml::font_style_italic;
            lf.lfUnderline = (descr.decoration_line & litehtml::text_decoration_line_underline) != 0;
            lf.lfStrikeOut = (descr.decoration_line & litehtml::text_decoration_line_line_through) != 0;
            lf.lfCharSet   = DEFAULT_CHARSET;

            // Every refresh builds a new document. Fonts are kept across documents, because
            // creating GDI fonts is a large part of the layout time.
            std::wstring key = std::wstring(lf.lfFaceName) + L"|" + std::to_wstring(lf.lfHeight) + L"|" +
                               std::to_wstring(lf.lfWeight) + L"|" + std::to_wstring(lf.lfItalic) +
                               std::to_wstring(lf.lfUnderline) + std::to_wstring(lf.lfStrikeOut);
            auto cached = m_fonts.find(key);
            if (cached != m_fonts.end())
            {
                if (fm)
                {
                    *fm           = cached->second.metrics;
                    fm->font_size = descr.size;
                }
                return (litehtml::uint_ptr)cached->second.font;
            }

            HFONT font = CreateFontIndirectW(&lf);
            CachedFont entry;
            entry.font = font;
            litehtml::font_metrics* out = fm ? fm : &entry.metrics;
            {
                HGDIOBJ    old = SelectObject(m_hdc, font);
                TEXTMETRICW tm = {};
                GetTextMetricsW(m_hdc, &tm);
                SIZE zero = {}, x = {};
                GetTextExtentPoint32W(m_hdc, L"0", 1, &zero);
                GetTextExtentPoint32W(m_hdc, L"x", 1, &x);
                SelectObject(m_hdc, old);

                out->font_size   = descr.size;
                out->ascent      = (int)tm.tmAscent;
                out->descent     = (int)tm.tmDescent;
                out->height      = (int)tm.tmHeight;
                out->x_height    = (int)tm.tmAscent / 2;
                out->ch_width    = (int)zero.cx;
                out->draw_spaces = lf.lfUnderline || lf.lfStrikeOut;
                out->sub_shift   = descr.size / 5;
                out->super_shift = descr.size / 3;
            }
            entry.metrics = *out;
            m_fonts[key]  = entry;
            return (litehtml::uint_ptr)font;
        }

        // Fonts stay in the cache until the app ends.
        void delete_font(litehtml::uint_ptr) override {}

        litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr font) override
        {
            std::wstring w   = Widen(text);
            HGDIOBJ      old = SelectObject(m_hdc, (HFONT)font);
            SIZE         sz  = {};
            GetTextExtentPoint32W(m_hdc, w.c_str(), (int)w.size(), &sz);
            SelectObject(m_hdc, old);
            return (int)sz.cx;
        }

        void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr font, litehtml::web_color color,
                       const litehtml::position& pos) override
        {
            HDC dc    = (HDC)hdc;
            int saved = BeginClip(dc);
            HGDIOBJ old = SelectObject(dc, (HFONT)font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, ToColorRef(color));
            std::wstring w  = Widen(text);
            RECT         rc = {(int)pos.left(), (int)pos.top(), (int)pos.right(), (int)pos.bottom()};
            DrawTextW(dc, w.c_str(), (int)w.size(), &rc, DT_SINGLELINE | DT_NOPREFIX | DT_BOTTOM | DT_NOCLIP);
            SelectObject(dc, old);
            RestoreDC(dc, saved);
        }

        litehtml::pixel_t pt_to_px(float pt) const override
        {
            return pt * 96.0f / 72.0f;
        }

        litehtml::pixel_t get_default_font_size() const override
        {
            return 14;
        }

        const char* get_default_font_name() const override
        {
            return "Arial";
        }

        void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) override
        {
            HDC dc    = (HDC)hdc;
            int saved = BeginClip(dc);
            int size  = (int)marker.pos.height / 3;
            FillBox(dc, (int)marker.pos.x, (int)(marker.pos.y + marker.pos.height / 3), size, size, marker.color);
            RestoreDC(dc, saved);
        }

        // Images: SVG only, parsed with nanosvg and rasterized per drawn size.
        void load_image(const char* src, const char* baseurl, bool) override
        {
            std::string url = ImageUrl(src, baseurl);
            if (m_images.count(url))
                return;
            double       t0  = NowMs();
            auto         img = std::make_unique<SvgImage>();
            HttpResponse resp;
            std::string  error;
            if (HttpGet(url, resp, error) && resp.status == 200 && resp.body.find("<svg") != std::string::npos)
            {
                std::vector<char> text(resp.body.begin(), resp.body.end());
                text.push_back('\0');
                img->svg = nsvgParse(text.data(), "px", 96.0f);
            }
            m_images[url] = std::move(img);
            g_msImages += NowMs() - t0;
        }

        void get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override
        {
            sz.width  = 0;
            sz.height = 0;
            auto it   = m_images.find(ImageUrl(src, baseurl));
            if (it != m_images.end() && it->second->svg)
            {
                sz.width  = (int)it->second->svg->width;
                sz.height = (int)it->second->svg->height;
            }
        }

        void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const std::string& url,
                        const std::string& base_url) override
        {
            auto it = m_images.find(ImageUrl(url.c_str(), base_url.c_str()));
            if (it == m_images.end() || !it->second->svg)
                return;
            SvgImage& img = *it->second;
            int       x = (int)layer.origin_box.x, y = (int)layer.origin_box.y;
            int       w = (int)layer.origin_box.width, h = (int)layer.origin_box.height;
            if (w <= 0 || h <= 0 || img.svg->width <= 0 || img.svg->height <= 0)
                return;

            if (img.rw != w || img.rh != h)
            {
                img.rgba.assign((size_t)w * h * 4, 0);
                float sx = w / img.svg->width, sy = h / img.svg->height;
                nsvgRasterize(m_rast, img.svg, 0, 0, sx < sy ? sx : sy, img.rgba.data(), w, h, w * 4);
                img.rw = w;
                img.rh = h;
            }

            // NT 4 has no AlphaBlend: copy the target area into a 32-bit DIB, blend there, copy back.
            BITMAPINFO bi              = {};
            bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
            bi.bmiHeader.biWidth       = w;
            bi.bmiHeader.biHeight      = -h;
            bi.bmiHeader.biPlanes      = 1;
            bi.bmiHeader.biBitCount    = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            void*   bits = nullptr;
            HBITMAP dib  = CreateDIBSection((HDC)hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!dib)
                return;
            HDC     mem = CreateCompatibleDC((HDC)hdc);
            HGDIOBJ old = SelectObject(mem, dib);
            BitBlt(mem, 0, 0, w, h, (HDC)hdc, x, y, SRCCOPY);
            GdiFlush();

            unsigned char*       dst = (unsigned char*)bits;
            const unsigned char* src = img.rgba.data();
            for (int i = 0; i < w * h; ++i, dst += 4, src += 4)
            {
                unsigned a = src[3];
                if (!a)
                    continue;
                dst[0] = (unsigned char)((src[2] * a + dst[0] * (255 - a)) / 255);
                dst[1] = (unsigned char)((src[1] * a + dst[1] * (255 - a)) / 255);
                dst[2] = (unsigned char)((src[0] * a + dst[2] * (255 - a)) / 255);
            }

            int saved = BeginLayerClip((HDC)hdc, layer);
            BitBlt((HDC)hdc, x, y, w, h, mem, 0, 0, SRCCOPY);
            RestoreDC((HDC)hdc, saved);
            SelectObject(mem, old);
            DeleteDC(mem);
            DeleteObject(dib);
        }

        void ClearImages()
        {
            m_images.clear();
        }

        void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                             const litehtml::web_color& color) override
        {
            HDC dc    = (HDC)hdc;
            int saved = BeginLayerClip(dc, layer);
            if (layer.is_root)
                FillBox(dc, 0, 0, g_clientW, g_clientH, color);
            else
                FillBox(dc, (int)layer.border_box.x, (int)layer.border_box.y, (int)layer.border_box.width,
                        (int)layer.border_box.height, color);
            RestoreDC(dc, saved);
        }

        // Linear gradients are drawn as 2 px strips along the main axis. Enough for bars and headers.
        void draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                  const litehtml::background_layer::linear_gradient& gradient) override
        {
            const auto& pts = gradient.color_points;
            if (pts.empty())
                return;
            HDC dc    = (HDC)hdc;
            int saved = BeginLayerClip(dc, layer);

            const auto& box   = layer.border_box;
            float       dx    = gradient.end.x - gradient.start.x;
            float       dy    = gradient.end.y - gradient.start.y;
            bool        horiz = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
            bool        flip  = horiz ? dx < 0 : dy < 0;
            int         len   = (int)(horiz ? box.width : box.height);

            for (int i = 0; i < len; i += 2)
            {
                float t = len > 1 ? (float)i / (float)(len - 1) : 0.0f;
                if (flip)
                    t = 1.0f - t;
                litehtml::web_color c = ColorAt(pts, t);
                if (horiz)
                    FillBox(dc, (int)box.x + i, (int)box.y, 2, (int)box.height, c);
                else
                    FillBox(dc, (int)box.x, (int)box.y + i, (int)box.width, 2, c);
            }
            RestoreDC(dc, saved);
        }

        void draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                  const litehtml::background_layer::radial_gradient& gradient) override
        {
            if (!gradient.color_points.empty())
                draw_solid_fill(hdc, layer, gradient.color_points.front().color);
        }

        void draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                                 const litehtml::background_layer::conic_gradient& gradient) override
        {
            if (!gradient.color_points.empty())
                draw_solid_fill(hdc, layer, gradient.color_points.front().color);
        }

        void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& b, const litehtml::position& p,
                          bool) override
        {
            HDC dc    = (HDC)hdc;
            int saved = BeginClip(dc);
            int x = (int)p.x, y = (int)p.y, w = (int)p.width, h = (int)p.height;
            if (Visible(b.top))
                FillBox(dc, x, y, w, (int)b.top.width, b.top.color);
            if (Visible(b.bottom))
                FillBox(dc, x, y + h - (int)b.bottom.width, w, (int)b.bottom.width, b.bottom.color);
            if (Visible(b.left))
                FillBox(dc, x, y, (int)b.left.width, h, b.left.color);
            if (Visible(b.right))
                FillBox(dc, x + w - (int)b.right.width, y, (int)b.right.width, h, b.right.color);
            RestoreDC(dc, saved);
        }

        void set_caption(const char* caption) override
        {
            g_title = std::string("Stykker Lite - ") + caption;
        }

        void set_base_url(const char* base_url) override
        {
            g_baseUrl = ResolveUrl(g_url, base_url);
        }

        void link(const std::shared_ptr<litehtml::document>&, const litehtml::element::ptr&) override {}

        void on_anchor_click(const char* url, const litehtml::element::ptr&) override
        {
            // Navigation replaces the document, so it must not happen inside a litehtml callback.
            g_pendingUrl = ResolveUrl(g_baseUrl, url);
            PostMessageW(g_hwnd, WM_APP_NAVIGATE, 0, 0);
        }

        void on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) override {}

        void set_cursor(const char* cursor) override
        {
            m_cursor = cursor ? cursor : "";
        }

        void transform_text(std::string& text, litehtml::text_transform tt) override
        {
            std::wstring w = Widen(text);
            if (w.empty())
                return;
            if (tt == litehtml::text_transform_uppercase)
                CharUpperBuffW(&w[0], (DWORD)w.size());
            else if (tt == litehtml::text_transform_lowercase)
                CharLowerBuffW(&w[0], (DWORD)w.size());
            else if (tt == litehtml::text_transform_capitalize)
                CharUpperBuffW(&w[0], 1);
            text = Narrow(w);
        }

        void import_css(std::string& text, const std::string& url, std::string& baseurl) override
        {
            std::string  full = ResolveUrl(baseurl.empty() ? g_baseUrl : baseurl, url);
            HttpResponse resp;
            std::string  error;
            if (HttpGet(full, resp, error) && resp.status == 200)
            {
                text    = resp.body;
                baseurl = full;
            }
        }

        void set_clip(const litehtml::position& pos, const litehtml::border_radiuses&) override
        {
            m_clips.push_back(pos);
        }

        void del_clip() override
        {
            if (!m_clips.empty())
                m_clips.pop_back();
        }

        void get_viewport(litehtml::position& viewport) const override
        {
            viewport = litehtml::position(0, 0, g_clientW, g_clientH);
        }

        litehtml::element::ptr create_element(const char*, const litehtml::string_map&,
                                              const std::shared_ptr<litehtml::document>&) override
        {
            return nullptr;
        }

        void get_media_features(litehtml::media_features& media) const override
        {
            media.type          = litehtml::media_type_screen;
            media.width         = g_clientW;
            media.height        = g_clientH;
            media.device_width  = GetSystemMetrics(SM_CXSCREEN);
            media.device_height = GetSystemMetrics(SM_CYSCREEN);
            media.color         = 8;
            media.monochrome    = 0;
            media.color_index   = 0;
            media.resolution    = 96;
        }

        void get_language(std::string& language, std::string& culture) const override
        {
            language = "de";
            culture  = "";
        }

        bool PointerCursor() const
        {
            return m_cursor == "pointer";
        }

      private:
        static int CALLBACK OnFontFamily(const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM param)
        {
            ((GdiContainer*)param)->m_faces.insert(Lower(Narrow(lf->lfFaceName)));
            return 1;
        }

        // First family in the CSS list that exists on this machine; generic names map to NT 4 fonts.
        std::wstring PickFace(const std::string& families) const
        {
            size_t start = 0;
            while (start <= families.size())
            {
                size_t      comma = families.find(',', start);
                std::string name  = Trim(families.substr(start, comma == std::string::npos ? std::string::npos
                                                                                          : comma - start));
                std::string key   = Lower(name);
                if (key == "sans-serif" || key == "system-ui")
                    return L"Arial";
                if (key == "serif")
                    return L"Times New Roman";
                if (key == "monospace")
                    return L"Courier New";
                if (!key.empty() && m_faces.count(key))
                    return Widen(name);
                if (comma == std::string::npos)
                    break;
                start = comma + 1;
            }
            return L"Arial";
        }

        int BeginClip(HDC dc)
        {
            int saved = SaveDC(dc);
            if (!m_clips.empty())
            {
                const auto& c = m_clips.back();
                IntersectClipRect(dc, (int)c.left(), (int)c.top(), (int)c.right(), (int)c.bottom());
            }
            return saved;
        }

        int BeginLayerClip(HDC dc, const litehtml::background_layer& layer)
        {
            int saved = BeginClip(dc);
            if (!layer.is_root)
                IntersectClipRect(dc, (int)layer.clip_box.left(), (int)layer.clip_box.top(),
                                  (int)layer.clip_box.right(), (int)layer.clip_box.bottom());
            return saved;
        }

        static bool Visible(const litehtml::border& b)
        {
            return b.width.value() > 0 && b.style != litehtml::border_style_none && b.style != litehtml::border_style_hidden;
        }

        static litehtml::web_color ColorAt(const std::vector<litehtml::background_layer::color_point>& pts, float t)
        {
            if (t <= pts.front().offset)
                return pts.front().color;
            for (size_t i = 1; i < pts.size(); ++i)
            {
                if (t <= pts[i].offset)
                {
                    float span = pts[i].offset - pts[i - 1].offset;
                    float f    = span > 0 ? (t - pts[i - 1].offset) / span : 1.0f;
                    return Mix(pts[i - 1].color, pts[i].color, f);
                }
            }
            return pts.back().color;
        }

        struct SvgImage
        {
            NSVGimage*                 svg = nullptr;
            std::vector<unsigned char> rgba;
            int                        rw = 0, rh = 0;
            ~SvgImage()
            {
                if (svg)
                    nsvgDelete(svg);
            }
        };

        static std::string ImageUrl(const char* src, const char* baseurl)
        {
            return ResolveUrl(baseurl && *baseurl ? baseurl : g_baseUrl, src ? src : "");
        }

        struct CachedFont
        {
            HFONT                  font = nullptr;
            litehtml::font_metrics metrics;
        };

        std::map<std::wstring, CachedFont>               m_fonts;
        std::map<std::string, std::unique_ptr<SvgImage>> m_images;
        NSVGrasterizer*                m_rast = nsvgCreateRasterizer();
        HDC                            m_hdc;
        std::set<std::string>          m_faces;
        std::vector<litehtml::position> m_clips;
        std::string                    m_cursor;
    };

    GdiContainer* g_container = nullptr;

    // ---------------------------------------------------------------- navigation

    std::string ErrorPage(const std::string& url, const std::string& error)
    {
        return "<html><head><title>Fehler</title></head>"
               "<body style=\"background:#060916;color:#e8eeff;font-family:Arial;padding:16px\">"
               "<p style=\"color:#4fe3ff;font-weight:bold;letter-spacing:3px;font-size:11px\">STYKKER LITE</p>"
               "<h1 style=\"font-size:22px\">Seite nicht erreichbar</h1>"
               "<p>" + url + "</p><p style=\"color:#8292b8\">" + error + "</p>"
               "<p style=\"color:#8292b8\">F5 versucht es erneut. Neuer Versuch in 5 Sekunden.</p>"
               "</body></html>";
    }

    // Reads "N" or "N;url=..." from a meta refresh or Refresh header.
    void ScheduleRefresh(const std::string& value)
    {
        KillTimer(g_hwnd, kRefreshTimer);
        g_refreshUrl.clear();
        if (value.empty())
            return;
        float  seconds = (float)atof(value.c_str());
        size_t urlPos  = Lower(value).find("url=");
        g_refreshUrl   = urlPos == std::string::npos ? g_url : ResolveUrl(g_baseUrl, Trim(value.substr(urlPos + 4)));
        UINT ms        = seconds < 0.02f ? 20 : (UINT)(seconds * 1000.0f);
        SetTimer(g_hwnd, kRefreshTimer, ms, nullptr);
    }

    void Layout()
    {
        if (g_doc)
        {
            g_doc->render(g_clientW);
            int maxScroll = (int)g_doc->height() - g_clientH;
            if (g_scrollY > maxScroll)
                g_scrollY = maxScroll > 0 ? maxScroll : 0;
        }
    }

    void RenderFrame();

    void Navigate(const std::string& url, bool keepScroll)
    {
        double start = NowMs();
        g_msCycle    = g_lastNavigate ? start - (double)g_lastNavigate : 0;
        g_lastNavigate = (LONGLONG)start;

        HCURSOR      oldCursor = SetCursor(LoadCursor(nullptr, IDC_WAIT));
        HttpResponse resp;
        std::string  error;
        bool         ok = HttpGet(url, resp, error);
        SetCursor(oldCursor);
        g_msFetch  = NowMs() - start;
        g_pageHttp = g_lastHttp;

        g_url     = ok ? resp.finalUrl : url;
        g_baseUrl = g_url;
        if (!keepScroll)
            g_scrollY = 0;

        std::string html = ok ? resp.body : ErrorPage(url, error);
        if (ok && resp.status != 200)
            html = ErrorPage(url, "HTTP " + std::to_string(resp.status));

        double layoutStart = NowMs();
        g_msImages         = 0;
        g_container->ClearImages();
        g_doc = litehtml::document::createFromString(html, g_container);
        g_msParse = NowMs() - layoutStart - g_msImages;
        Layout();
        g_msLayout = NowMs() - layoutStart - g_msImages - g_msParse;

        std::string refresh;
        if (ok && resp.status == 200)
        {
            refresh = HeaderValue(resp.headers, "Refresh");
            if (auto meta = g_doc->root()->select_one("meta[http-equiv=refresh]"))
                refresh = meta->get_attr("content", "");
        }
        else
        {
            refresh = "5;url=" + url;
        }
        // Draw now, so the refresh interval starts after the frame is on screen.
        RenderFrame();
        ScheduleRefresh(refresh);
        UpdateTitle();
    }

    // Keyboard access to links: a key activates the link with the matching accesskey attribute.
    bool ActivateAccessKey(wchar_t ch)
    {
        if (!g_doc || ch < 32 || ch > 126)
            return false;
        char        key      = (char)(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
        std::string selector = std::string("a[accesskey=\"") + key + "\"]";
        auto        el       = g_doc->root()->select_one(selector);
        if (!el)
            return false;
        g_pendingUrl = ResolveUrl(g_baseUrl, el->get_attr("href", ""));
        PostMessageW(g_hwnd, WM_APP_NAVIGATE, 0, 0);
        return true;
    }

    // ---------------------------------------------------------------- frame buffers
    //
    // Two 32-bit DIBs: "front" holds what is on screen, "back" gets the next frame.
    // After drawing, only the rows/columns that differ are copied to the window. Writes to
    // the graphics memory are the slow part in the VM, and live values change only a few pixels.

    struct Frame
    {
        HDC      dc     = nullptr;
        HBITMAP  bmp    = nullptr;
        HGDIOBJ  oldBmp = nullptr;
        DWORD*   bits   = nullptr;
    };

    Frame g_front, g_back;
    int   g_frameW = 0, g_frameH = 0;
    bool  g_frontValid = false;

    void FreeFrame(Frame& f)
    {
        if (f.dc)
        {
            SelectObject(f.dc, f.oldBmp);
            DeleteDC(f.dc);
            DeleteObject(f.bmp);
        }
        f = Frame();
    }

    bool MakeFrame(Frame& f, int w, int h)
    {
        BITMAPINFO bi              = {};
        bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth       = w;
        bi.bmiHeader.biHeight      = -h;
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        f.bmp      = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!f.bmp)
            return false;
        f.dc     = CreateCompatibleDC(nullptr);
        f.oldBmp = SelectObject(f.dc, f.bmp);
        f.bits   = (DWORD*)bits;
        return true;
    }

    void EnsureFrames()
    {
        if (g_frameW == g_clientW && g_frameH == g_clientH && g_front.dc)
            return;
        FreeFrame(g_front);
        FreeFrame(g_back);
        g_frameW = g_clientW;
        g_frameH = g_clientH;
        g_frontValid = false;
        if (g_frameW > 0 && g_frameH > 0)
        {
            MakeFrame(g_front, g_frameW, g_frameH);
            MakeFrame(g_back, g_frameW, g_frameH);
        }
    }

    // Draws the document into the back buffer, copies the changed parts to the window, swaps buffers.
    void RenderFrame()
    {
        EnsureFrames();
        if (!g_back.dc)
            return;

        double start = NowMs();
        RECT   all   = {0, 0, g_frameW, g_frameH};
        HBRUSH bg    = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(g_back.dc, &all, bg);
        DeleteObject(bg);
        if (g_doc)
        {
            litehtml::position clip(0, 0, g_frameW, g_frameH);
            g_doc->draw((litehtml::uint_ptr)g_back.dc, 0, -g_scrollY, &clip);
        }
        GdiFlush();
        g_msDraw = NowMs() - start;

        start      = NowMs();
        long   pixels = 0;
        HDC    hdc    = GetDC(g_hwnd);
        if (!g_frontValid)
        {
            BitBlt(hdc, 0, 0, g_frameW, g_frameH, g_back.dc, 0, 0, SRCCOPY);
            pixels = (long)g_frameW * g_frameH;
        }
        else
        {
            // Collect runs of changed rows into one rectangle each (union of their column ranges).
            int y = 0;
            while (y < g_frameH)
            {
                int top = -1, left = g_frameW, right = -1;
                for (; y < g_frameH; ++y)
                {
                    const DWORD* a = g_back.bits + (size_t)y * g_frameW;
                    const DWORD* b = g_front.bits + (size_t)y * g_frameW;
                    int          l = 0, r = g_frameW - 1;
                    while (l <= r && a[l] == b[l])
                        ++l;
                    if (l > r)
                    {
                        if (top >= 0)
                            break; // end of this run
                        continue;
                    }
                    while (a[r] == b[r])
                        --r;
                    if (top < 0)
                        top = y;
                    if (l < left)
                        left = l;
                    if (r > right)
                        right = r;
                }
                if (top >= 0)
                {
                    int w = right - left + 1, h = y - top;
                    BitBlt(hdc, left, top, w, h, g_back.dc, left, top, SRCCOPY);
                    pixels += (long)w * h;
                }
            }
        }
        ReleaseDC(g_hwnd, hdc);
        g_msBlit  = NowMs() - start;
        g_blitKpx = (int)(pixels / 1000);

        Frame t     = g_front;
        g_front     = g_back;
        g_back      = t;
        g_frontValid = true;
    }

    // Window exposed by another window: copy from the front buffer, no redraw needed.
    void PaintFromFront(HDC hdc, const RECT& rc)
    {
        if (g_frontValid)
            BitBlt(hdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, g_front.dc, rc.left, rc.top,
                   SRCCOPY);
        else
            RenderFrame();
    }

    void ScrollBy(int dy)
    {
        if (!g_doc)
            return;
        int maxScroll = (int)g_doc->height() - g_clientH;
        g_scrollY += dy;
        if (g_scrollY > maxScroll)
            g_scrollY = maxScroll;
        if (g_scrollY < 0)
            g_scrollY = 0;
        RenderFrame();
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // Hover/click changes from litehtml: redraw after the callback returns.
        auto redrawAll = [](const litehtml::position&) { PostMessageW(g_hwnd, WM_APP_REDRAW, 0, 0); };

        switch (msg)
        {
        case WM_SIZE:
            g_clientW = LOWORD(lParam);
            g_clientH = HIWORD(lParam);
            Layout();
            if (g_clientW > 0 && g_clientH > 0)
                RenderFrame();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC         hdc = BeginPaint(hwnd, &ps);
            PaintFromFront(hdc, ps.rcPaint);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_APP_REDRAW:
        {
            MSG dup; // several hover changes in a row need only one redraw
            while (PeekMessageW(&dup, hwnd, WM_APP_REDRAW, WM_APP_REDRAW, PM_REMOVE))
            {
            }
            RenderFrame();
            return 0;
        }

        case WM_MOUSEMOVE:
            if (g_doc)
            {
                int x = (short)LOWORD(lParam), y = (short)HIWORD(lParam);
                g_doc->on_mouse_over(x, y + g_scrollY, x, y, redrawAll);
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (g_doc)
            {
                int x = (short)LOWORD(lParam), y = (short)HIWORD(lParam);
                g_doc->on_lbutton_down(x, y + g_scrollY, x, y, redrawAll);
            }
            return 0;

        case WM_LBUTTONUP:
            if (g_doc)
            {
                int x = (short)LOWORD(lParam), y = (short)HIWORD(lParam);
                g_doc->on_lbutton_up(x, y + g_scrollY, x, y, redrawAll);
            }
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && g_container)
            {
                // IDC_HAND does not exist on NT 4; fall back to the arrow.
                HCURSOR hand = g_container->PointerCursor() ? LoadCursor(nullptr, MAKEINTRESOURCE(32649)) : nullptr;
                SetCursor(hand ? hand : LoadCursor(nullptr, IDC_ARROW));
                return TRUE;
            }
            break;

        case WM_KEYDOWN:
            switch (wParam)
            {
            case VK_ESCAPE:
                DestroyWindow(hwnd);
                return 0;
            case VK_F5:
                Navigate(g_url, true);
                return 0;
            case VK_DOWN:
                ScrollBy(40);
                return 0;
            case VK_UP:
                ScrollBy(-40);
                return 0;
            case VK_NEXT:
                ScrollBy(g_clientH - 40);
                return 0;
            case VK_PRIOR:
                ScrollBy(-(g_clientH - 40));
                return 0;
            }
            break;

        case WM_CHAR:
            ActivateAccessKey((wchar_t)wParam);
            return 0;

        case WM_TIMER:
            if (wParam == kRefreshTimer)
            {
                KillTimer(hwnd, kRefreshTimer);
                std::string target = g_refreshUrl;
                Navigate(target, target == g_url);
            }
            return 0;

        case WM_APP_NAVIGATE:
        {
            std::string target = g_pendingUrl;
            g_pendingUrl.clear();
            if (!target.empty())
                Navigate(target, false);
            return 0;
        }

        case WM_DESTROY:
            KillTimer(hwnd, kRefreshTimer);
            g_doc.reset();
            FreeFrame(g_front);
            FreeFrame(g_back);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR cmdLine, int nCmdShow)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(1, 1), &wsa) != 0)
    {
        MessageBoxW(nullptr, L"Winsock konnte nicht gestartet werden.", L"Stykker Lite", MB_ICONERROR);
        return 1;
    }

    std::string startUrl = Trim(cmdLine ? cmdLine : "");
    if (startUrl.empty())
        startUrl = kDefaultUrl;

    // "-bench <file>": parse a local HTML file 20 times and report the average (for profiling).
    if (startUrl.compare(0, 7, "-bench ") == 0)
    {
        std::string file = Trim(startUrl.substr(7));
        HANDLE      h    = CreateFileA(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        std::string html;
        if (h != INVALID_HANDLE_VALUE)
        {
            char  buf[4096];
            DWORD n;
            while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n)
                html.append(buf, n);
            CloseHandle(h);
        }
        GdiContainer bench;
        g_container = &bench;
        g_clientW   = 620;
        g_clientH   = 470;
        double t0   = NowMs();
        for (int i = 0; i < 20; ++i)
            litehtml::document::createFromString(html, &bench);
        double t1 = NowMs();
        auto   doc = litehtml::document::createFromString(html, &bench);
        double t2  = NowMs();
        for (int i = 0; i < 20; ++i)
            doc->render(620);
        double t3 = NowMs();
        char   msg[200];
        snprintf(msg, sizeof(msg), "%s\nparse %.2f ms, render %.2f ms (avg of 20), %d bytes", file.c_str(),
                 (t1 - t0) / 20, (t3 - t2) / 20, (int)html.size());
        HANDLE out = CreateFileA((file + ".txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        DWORD  written;
        WriteFile(out, msg, (DWORD)strlen(msg), &written, nullptr);
        CloseHandle(out);
        return 0;
    }

    WNDCLASSW wc     = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kClassName;
    RegisterClassW(&wc);

    RECT rc = {0, 0, 620, 470};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowW(kClassName, L"Stykker Lite", WS_OVERLAPPEDWINDOW, 0, 0,
                           rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInstance, nullptr);

    GdiContainer container;
    g_container = &container;

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);
    Navigate(startUrl, false);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_container = nullptr;
    WSACleanup();
    return (int)msg.wParam;
}
