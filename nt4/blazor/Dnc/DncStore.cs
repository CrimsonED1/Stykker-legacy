using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Extensions.Options;

namespace Nt4Blazor.Dnc;

public sealed record DncProgram(string Name, long Size, DateTime Modified, uint Crc);

// File-based program store: <ProgramRoot>/<machine>/<program>. Only plain files directly in the
// machine folder are offered, no subfolders.
public sealed partial class DncStore
{
    private readonly DncOptions _options;
    private readonly string _root;
    private readonly string _logFile;
    private readonly object _logLock = new();

    public DncStore(IOptions<DncOptions> options, IWebHostEnvironment env)
    {
        _options = options.Value;
        _root = Path.GetFullPath(Path.Combine(env.ContentRootPath, _options.ProgramRoot));
        _logFile = Path.GetFullPath(Path.Combine(env.ContentRootPath, _options.LogFile));
    }

    // Program names as the client may write them: letters, digits, _ . -, no path parts.
    // 840D names like TEIL1.MPF or L100.SPF fit.
    [GeneratedRegex(@"^[A-Za-z0-9_][A-Za-z0-9_.\-]{0,63}$")]
    private static partial Regex ProgramNameRegex();

    [GeneratedRegex(@"^[A-Za-z0-9_\-]{1,32}$")]
    private static partial Regex MachineNameRegex();

    public static bool IsValidProgramName(string? name) =>
        name is not null && ProgramNameRegex().IsMatch(name) && !name.Contains("..");

    // Checks machine name and key from the request headers. The key comparison takes the same time
    // whether it matches or not.
    public bool TryAuthenticate(HttpRequest request, out string machine)
    {
        machine = request.Headers["X-Nt4-Machine"].ToString();
        string token = request.Headers["X-Nt4-Token"].ToString();
        if (!MachineNameRegex().IsMatch(machine) || token.Length == 0)
            return false;
        if (!_options.Machines.TryGetValue(machine, out var expected) || string.IsNullOrEmpty(expected))
            return false;
        return CryptographicOperations.FixedTimeEquals(Encoding.UTF8.GetBytes(token), Encoding.UTF8.GetBytes(expected));
    }

    private string MachineDir(string machine) => Path.Combine(_root, machine.ToUpperInvariant());

    public IReadOnlyList<DncProgram> List(string machine)
    {
        var dir = MachineDir(machine);
        if (!Directory.Exists(dir))
            return [];
        var result = new List<DncProgram>();
        foreach (var path in Directory.EnumerateFiles(dir).Order(StringComparer.OrdinalIgnoreCase))
        {
            var name = Path.GetFileName(path);
            if (!IsValidProgramName(name))
                continue;
            var bytes = File.ReadAllBytes(path);
            result.Add(new DncProgram(name, bytes.Length, File.GetLastWriteTime(path), Crc32.Compute(bytes)));
        }
        return result;
    }

    // Reads a program once, so content and checksum always belong together.
    public byte[]? Read(string machine, string name)
    {
        if (!IsValidProgramName(name))
            return null;
        var dir = MachineDir(machine);
        var path = Path.GetFullPath(Path.Combine(dir, name));
        if (!path.StartsWith(dir + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase) || !File.Exists(path))
            return null;
        return File.ReadAllBytes(path);
    }

    public void Log(string machine, string remote, string text)
    {
        // Only printable ASCII from the client, so the log stays one line per event.
        var clean = new StringBuilder(text.Length);
        foreach (char c in text)
            clean.Append(c >= ' ' && c < 127 ? c : ' ');
        var line = $"{DateTime.Now:yyyy-MM-dd HH:mm:ss};{machine};{remote};{clean.ToString().Trim()}{Environment.NewLine}";
        lock (_logLock)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_logFile)!);
            File.AppendAllText(_logFile, line);
        }
    }
}
