namespace Nt4Blazor.Dnc;

// Section "Dnc" in appsettings. Real machine names and keys do not belong in the repo:
// set them on the server, e.g. as environment variable Dnc__Machines__M12=<key>.
public sealed class DncOptions
{
    // Programs per machine live in <ProgramRoot>/<machine>/. Relative paths start at the app folder.
    // Not "dnc/": on Windows that is the same folder as the source folder Dnc/.
    public string ProgramRoot { get; set; } = "dnc-daten/programme";

    // Transfer log, one line per event.
    public string LogFile { get; set; } = "dnc-daten/transfer.log";

    // Machine name -> key. A machine that is not listed gets no access.
    public Dictionary<string, string> Machines { get; set; } = new(StringComparer.OrdinalIgnoreCase);
}
