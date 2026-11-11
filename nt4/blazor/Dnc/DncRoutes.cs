using System.Globalization;
using System.Text;

namespace Nt4Blazor.Dnc;

// Plain-text API for NT4DNC.EXE. No JSON, no TLS: NT 4 reads this with Winsock and a few lines of C.
// Every request carries the headers X-Nt4-Machine and X-Nt4-Token.
//
//   GET  /nt4/api/dnc/list         one line per program: NAME;SIZE;YYYYMMDDhhmmss;CRC32 (hex)
//   GET  /nt4/api/dnc/file/{name}  the program unchanged (bytes), header X-Crc32
//   POST /nt4/api/dnc/log          one line of text from the client, goes into the transfer log
public static class DncRoutes
{
    private const string Ascii = "text/plain; charset=us-ascii";

    public static void Map(WebApplication app)
    {
        var api = app.MapGroup("/nt4/api/dnc");

        api.MapGet("/list", (HttpRequest request, DncStore store) =>
        {
            if (!store.TryAuthenticate(request, out var machine))
                return Denied();
            var sb = new StringBuilder();
            foreach (var p in store.List(machine))
            {
                sb.Append(p.Name).Append(';')
                  .Append(p.Size.ToString(CultureInfo.InvariantCulture)).Append(';')
                  .Append(p.Modified.ToString("yyyyMMddHHmmss", CultureInfo.InvariantCulture)).Append(';')
                  .Append(p.Crc.ToString("X8", CultureInfo.InvariantCulture)).Append("\r\n");
            }
            return Results.Text(sb.ToString(), Ascii);
        });

        api.MapGet("/file/{name}", (string name, HttpRequest request, HttpResponse response, DncStore store) =>
        {
            if (!store.TryAuthenticate(request, out var machine))
                return Denied();
            var bytes = store.Read(machine, name);
            if (bytes is null)
                return Results.Text("FEHLER Programm nicht gefunden\r\n", Ascii, statusCode: StatusCodes.Status404NotFound);
            response.Headers["X-Crc32"] = Crc32.Compute(bytes).ToString("X8", CultureInfo.InvariantCulture);
            return Results.Bytes(bytes, "application/octet-stream");
        });

        api.MapPost("/log", async (HttpRequest request, DncStore store) =>
        {
            if (!store.TryAuthenticate(request, out var machine))
                return Denied();
            var buffer = new byte[1024];
            int read = 0, n;
            while (read < buffer.Length && (n = await request.Body.ReadAsync(buffer.AsMemory(read))) > 0)
                read += n;
            var remote = request.HttpContext.Connection.RemoteIpAddress?.ToString() ?? "?";
            store.Log(machine, remote, Encoding.ASCII.GetString(buffer, 0, read));
            return Results.Text("OK\r\n", Ascii);
        });
    }

    private static IResult Denied() =>
        Results.Text("FEHLER Kein Zugang (Maschine oder Schluessel)\r\n", Ascii, statusCode: StatusCodes.Status401Unauthorized);
}
