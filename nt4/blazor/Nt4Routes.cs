using Microsoft.AspNetCore.Http.HttpResults;
using Nt4Blazor.Components.Lite;

namespace Nt4Blazor;

// All NT 4 endpoints under /nt4. The old /lite addresses stay as aliases,
// so NT4LITE.EXE builds that already run on the VM keep working.
public static class Nt4Routes
{
    public static void Map(WebApplication app)
    {
        foreach (var prefix in new[] { "/nt4", "/lite" })
            MapPrefix(app, prefix);

        // Plain text for native clients: no HTML parsing, no SVG.
        app.MapGet("/nt4/api/axes", () => Results.Text(Nt4Axes.AsText(DateTime.Now), "text/plain"));
    }

    private static void MapPrefix(WebApplication app, string prefix)
    {
        // Page with timer and charts. Commands redirect back so a refresh does not repeat them.
        app.MapGet(prefix, IResult (string? cmd, LiteTimer timer) =>
        {
            switch (cmd)
            {
                case "start": timer.Start(); break;
                case "stop": timer.Stop(); break;
                case "reset": timer.Reset(); break;
                default: return new RazorComponentResult<LitePage>();
            }
            return Results.Redirect("/nt4");
        });

        app.MapGet(prefix + "/line.svg", (LiteChartData data) =>
            Results.Text(data.LineSvg(560, 110), "image/svg+xml"));
        app.MapGet(prefix + "/bars.svg", (LiteChartData data) =>
            Results.Text(data.BarSvg(560, 90), "image/svg+xml"));

        // Axis view: only text and bars, so it can refresh fast. ?ms= sets the refresh interval.
        app.MapGet(prefix + "/axes", (int? ms) =>
            new RazorComponentResult<LiteAxes>(new { IntervalMs = Math.Clamp(ms ?? 200, 50, 5000) }));
    }
}
