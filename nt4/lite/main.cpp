// Stykker Lite: a small HTML view for Windows NT 4.0.
// Loads a page over plain HTTP (Winsock), renders it with litehtml onto GDI,
// follows links, and reloads when the page asks for it (meta refresh / Refresh header).
// No JavaScript: the server drives the page.

#include <winsock2.h>
#include <windows.h>

#include <litehtml.h>

#include <string>
#include <vector>
#include <set>
#include <memory>

namespace
{
    const wchar_t* kClassName  = L"StykkerNT4Lite";
    const char*    kDefaultUrl = "http://192.168.178.146:5080/lite";
    const UINT     WM_APP_NAVIGATE = WM_APP + 1;
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

    bool HttpGetOnce(const std::string& url, HttpResponse& resp, std::string& error)
    {
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
        send(s, request.c_str(), (int)request.size(), 0);

        std::string raw;
        char        buf[4096];
        for (;;)
        {
            int n = recv(s, buf, sizeof(buf), 0);
            if (n <= 0)
                break;
            raw.append(buf, n);
        }
        closesocket(s);

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
            HFONT font     = CreateFontIndirectW(&lf);

            if (fm)
            {
                HGDIOBJ    old = SelectObject(m_hdc, font);
                TEXTMETRICW tm = {};
                GetTextMetricsW(m_hdc, &tm);
                SIZE zero = {}, x = {};
                GetTextExtentPoint32W(m_hdc, L"0", 1, &zero);
                GetTextExtentPoint32W(m_hdc, L"x", 1, &x);
                SelectObject(m_hdc, old);

                fm->font_size   = descr.size;
                fm->ascent      = (int)tm.tmAscent;
                fm->descent     = (int)tm.tmDescent;
                fm->height      = (int)tm.tmHeight;
                fm->x_height    = (int)tm.tmAscent / 2;
                fm->ch_width    = (int)zero.cx;
                fm->draw_spaces = lf.lfUnderline || lf.lfStrikeOut;
                fm->sub_shift   = descr.size / 5;
                fm->super_shift = descr.size / 3;
            }
            return (litehtml::uint_ptr)font;
        }

        void delete_font(litehtml::uint_ptr font) override
        {
            DeleteObject((HFONT)font);
        }

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

        void load_image(const char*, const char*, bool) override {}

        void get_image_size(const char*, const char*, litehtml::size& sz) override
        {
            sz.width  = 0;
            sz.height = 0;
        }

        void draw_image(litehtml::uint_ptr, const litehtml::background_layer&, const std::string&,
                        const std::string&) override
        {
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
            SetWindowTextW(g_hwnd, (L"Stykker Lite - " + Widen(caption)).c_str());
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
        UINT ms        = seconds < 0.2f ? 200 : (UINT)(seconds * 1000.0f);
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

    void Navigate(const std::string& url, bool keepScroll)
    {
        HCURSOR      oldCursor = SetCursor(LoadCursor(nullptr, IDC_WAIT));
        HttpResponse resp;
        std::string  error;
        bool         ok = HttpGet(url, resp, error);
        SetCursor(oldCursor);

        g_url     = ok ? resp.finalUrl : url;
        g_baseUrl = g_url;
        if (!keepScroll)
            g_scrollY = 0;

        std::string html = ok ? resp.body : ErrorPage(url, error);
        if (ok && resp.status != 200)
            html = ErrorPage(url, "HTTP " + std::to_string(resp.status));

        g_doc = litehtml::document::createFromString(html, g_container);
        Layout();

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
        ScheduleRefresh(refresh);
        InvalidateRect(g_hwnd, nullptr, FALSE);
    }

    // Keyboard access to links: a key activates the link with the matching accesskey attribute.
    bool ActivateAccessKey(wchar_t ch)
    {
        if (!g_doc || ch < 32 || ch > 126)
            return false;
        char        key      = (char)(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
        std::string selector = std::string("a[accesskey=") + key + "]";
        auto        el       = g_doc->root()->select_one(selector);
        if (!el)
            return false;
        g_pendingUrl = ResolveUrl(g_baseUrl, el->get_attr("href", ""));
        PostMessageW(g_hwnd, WM_APP_NAVIGATE, 0, 0);
        return true;
    }

    void Paint(HDC hdc)
    {
        HDC     mem    = CreateCompatibleDC(hdc);
        HBITMAP bmp    = CreateCompatibleBitmap(hdc, g_clientW, g_clientH);
        HGDIOBJ oldBmp = SelectObject(mem, bmp);

        RECT   all = {0, 0, g_clientW, g_clientH};
        HBRUSH bg  = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(mem, &all, bg);
        DeleteObject(bg);

        if (g_doc)
        {
            litehtml::position clip(0, 0, g_clientW, g_clientH);
            g_doc->draw((litehtml::uint_ptr)mem, 0, -g_scrollY, &clip);
        }

        BitBlt(hdc, 0, 0, g_clientW, g_clientH, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
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
        InvalidateRect(g_hwnd, nullptr, FALSE);
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto redrawAll = [](const litehtml::position&) { InvalidateRect(g_hwnd, nullptr, FALSE); };

        switch (msg)
        {
        case WM_SIZE:
            g_clientW = LOWORD(lParam);
            g_clientH = HIWORD(lParam);
            Layout();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC         hdc = BeginPaint(hwnd, &ps);
            Paint(hdc);
            EndPaint(hwnd, &ps);
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

    WNDCLASSW wc     = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kClassName;
    RegisterClassW(&wc);

    RECT rc = {0, 0, 640, 440};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowW(kClassName, L"Stykker Lite", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
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
