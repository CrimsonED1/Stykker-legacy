using System.Globalization;

namespace Nt4Blazor;

// Axis positions for the NT 4 clients. Computed from the clock, so every request gets fresh values.
public sealed record Nt4Axis(string Name, string Unit, double Min, double Max, double Value)
{
    public int Percent => (int)Math.Round((Value - Min) / (Max - Min) * 100);
}

public static class Nt4Axes
{
    public static readonly int[] Speeds = [1000, 500, 200, 100, 50];

    public static Nt4Axis[] Read(DateTime now)
    {
        double t = now.TimeOfDay.TotalSeconds;
        return
        [
            new("X", "mm", -250, 250, 200 * Math.Sin(t * 0.7)),
            new("Y", "mm", -150, 150, 120 * Math.Cos(t * 0.5)),
            new("Z", "mm", 0, 100, 50 + 45 * Math.Sin(t * 1.3)),
            new("A", "°", 0, 360, (t * 40) % 360),
        ];
    }

    public static string Format(double v) => v.ToString("+0.000;-0.000", CultureInfo.InvariantCulture);

    // Plain text for the NT 4 native client, e.g. "X=+200.000;Y=-120.000;Z=+50.000;A=+40.000".
    // No JSON and no library needed on the client side.
    public static string AsText(DateTime now) =>
        string.Join(';', Read(now).Select(a => $"{a.Name}={Format(a.Value)}"));
}
