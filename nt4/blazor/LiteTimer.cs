using System.Diagnostics;

namespace Nt4Blazor;

// Server-side timer for the NT 4 lite page. The client only renders what the server reports.
public sealed class LiteTimer
{
    private readonly object _lock = new();
    private readonly Stopwatch _watch = new();

    public TimeSpan Duration { get; } = TimeSpan.FromSeconds(20);

    public void Start()
    {
        lock (_lock)
        {
            if (Elapsed >= Duration)
                _watch.Reset();
            _watch.Start();
        }
    }

    public void Stop()
    {
        lock (_lock)
            _watch.Stop();
    }

    public void Reset()
    {
        lock (_lock)
            _watch.Reset();
    }

    public TimeSpan Elapsed
    {
        get
        {
            lock (_lock)
            {
                var elapsed = _watch.Elapsed;
                if (elapsed >= Duration)
                {
                    _watch.Stop();
                    return Duration;
                }
                return elapsed;
            }
        }
    }

    public bool Running
    {
        get
        {
            lock (_lock)
                return _watch.IsRunning && _watch.Elapsed < Duration;
        }
    }

    public int Percent => (int)(Elapsed.TotalMilliseconds * 100 / Duration.TotalMilliseconds);
}
