using System.Globalization;
using System.Text;

namespace Nt4Blazor;

// Live demo data for the NT 4 lite page: a load curve sampled every second and six bars.
public sealed class LiteChartData : BackgroundService
{
    private const int Samples = 40;
    private readonly object _lock = new();
    private readonly Queue<double> _line = new();
    private readonly double[] _bars = new double[6];
    private readonly Random _random = new();
    private double _current = 50;
    private long _tick;

    public static readonly string[] BarLabels = ["Mo", "Di", "Mi", "Do", "Fr", "Sa"];

    public LiteChartData()
    {
        for (int i = 0; i < _bars.Length; i++)
            _bars[i] = 20 + _random.NextDouble() * 70;
        for (int i = 0; i < Samples; i++)
            Step();
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(1));
        while (await timer.WaitForNextTickAsync(stoppingToken))
            Step();
    }

    private void Step()
    {
        lock (_lock)
        {
            _current = Math.Clamp(_current + (_random.NextDouble() - 0.5) * 18, 5, 95);
            _line.Enqueue(_current);
            while (_line.Count > Samples)
                _line.Dequeue();
            if (++_tick % 3 == 0)
            {
                int i = _random.Next(_bars.Length);
                _bars[i] = Math.Clamp(_bars[i] + (_random.NextDouble() - 0.5) * 30, 5, 100);
            }
        }
    }

    public (double[] Line, double[] Bars, long Tick) Snapshot()
    {
        lock (_lock)
            return (_line.ToArray(), (double[])_bars.Clone(), _tick);
    }

    // SVG without text: nanosvg on NT 4 does not render <text>, labels live in the HTML.
    public string LineSvg(int width, int height)
    {
        var (line, _, _) = Snapshot();
        var sb = Begin(width, height);
        sb.Append("<defs><linearGradient id=\"area\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\">")
          .Append("<stop offset=\"0\" stop-color=\"#4fe3ff\" stop-opacity=\"0.45\"/>")
          .Append("<stop offset=\"1\" stop-color=\"#4fe3ff\" stop-opacity=\"0\"/></linearGradient></defs>");
        Grid(sb, width, height);

        var points = new StringBuilder();
        for (int i = 0; i < line.Length; i++)
        {
            double x = i * (width - 1.0) / (Samples - 1);
            double y = height - 4 - line[i] / 100.0 * (height - 8);
            points.Append(F(x)).Append(',').Append(F(y)).Append(' ');
        }
        double lastX = (line.Length - 1) * (width - 1.0) / (Samples - 1);
        sb.Append("<polygon fill=\"url(#area)\" points=\"0,").Append(height).Append(' ')
          .Append(points).Append(F(lastX)).Append(',').Append(height).Append("\"/>");
        sb.Append("<polyline fill=\"none\" stroke=\"#4fe3ff\" stroke-width=\"2\" stroke-linejoin=\"round\" points=\"")
          .Append(points).Append("\"/>");
        if (line.Length > 0)
        {
            double y = height - 4 - line[^1] / 100.0 * (height - 8);
            sb.Append("<circle cx=\"").Append(F(lastX)).Append("\" cy=\"").Append(F(y))
              .Append("\" r=\"4\" fill=\"#e8eeff\" stroke=\"#4fe3ff\" stroke-width=\"2\"/>");
        }
        return sb.Append("</svg>").ToString();
    }

    public string BarSvg(int width, int height)
    {
        var (_, bars, _) = Snapshot();
        var sb = Begin(width, height);
        sb.Append("<defs><linearGradient id=\"bar\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\">")
          .Append("<stop offset=\"0\" stop-color=\"#4fe3ff\"/><stop offset=\"1\" stop-color=\"#1f6f8b\"/>")
          .Append("</linearGradient></defs>");
        Grid(sb, width, height);
        double slot = width / (double)bars.Length;
        for (int i = 0; i < bars.Length; i++)
        {
            double h = bars[i] / 100.0 * (height - 8);
            sb.Append("<rect x=\"").Append(F(i * slot + slot * 0.2)).Append("\" y=\"").Append(F(height - h))
              .Append("\" width=\"").Append(F(slot * 0.6)).Append("\" height=\"").Append(F(h))
              .Append("\" rx=\"3\" fill=\"url(#bar)\"/>");
        }
        return sb.Append("</svg>").ToString();
    }

    private static StringBuilder Begin(int width, int height) =>
        new StringBuilder()
            .Append("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"").Append(width)
            .Append("\" height=\"").Append(height).Append("\" viewBox=\"0 0 ").Append(width).Append(' ')
            .Append(height).Append("\">")
            .Append("<rect x=\"0\" y=\"0\" width=\"").Append(width).Append("\" height=\"").Append(height)
            .Append("\" fill=\"#0b1226\"/>");

    private static void Grid(StringBuilder sb, int width, int height)
    {
        for (int i = 1; i < 4; i++)
        {
            double y = i * height / 4.0;
            sb.Append("<line x1=\"0\" y1=\"").Append(F(y)).Append("\" x2=\"").Append(width).Append("\" y2=\"")
              .Append(F(y)).Append("\" stroke=\"#23305a\" stroke-width=\"1\"/>");
        }
    }

    private static string F(double v) => v.ToString("0.#", CultureInfo.InvariantCulture);
}
