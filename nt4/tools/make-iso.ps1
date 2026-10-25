param(
    [Parameter(Mandatory)] [string]$SourceDir,
    [Parameter(Mandatory)] [string]$IsoPath,
    [string]$VolumeName = 'NT4APPS'
)

$ErrorActionPreference = 'Stop'

# IMAPI2 liefert einen COM-IStream. Den schreiben wir per Helfer in eine Datei.
Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;

public static class IsoStreamWriter
{
    public static void Save(object comStream, string path)
    {
        IStream s = (IStream)comStream;
        IntPtr read = Marshal.AllocHGlobal(4);
        byte[] buf = new byte[65536];
        try
        {
            using (FileStream fs = File.Create(path))
            {
                while (true)
                {
                    s.Read(buf, buf.Length, read);
                    int n = Marshal.ReadInt32(read);
                    if (n == 0) break;
                    fs.Write(buf, 0, n);
                }
            }
        }
        finally
        {
            Marshal.FreeHGlobal(read);
        }
    }
}
"@

$src = (Resolve-Path -LiteralPath $SourceDir).Path

$fsi = New-Object -ComObject IMAPI2FS.MsftFileSystemImage
$fsi.FileSystemsToCreate = 3   # ISO9660 + Joliet
$fsi.VolumeName = $VolumeName
# Zweiter Parameter false: Inhalt liegt direkt im Wurzelverzeichnis der ISO
$fsi.Root.AddTree($src, $false)

$result = $fsi.CreateResultImage()

if (Test-Path -LiteralPath $IsoPath) { Remove-Item -LiteralPath $IsoPath -Force }
[IsoStreamWriter]::Save($result.ImageStream, $IsoPath)

$item = Get-Item -LiteralPath $IsoPath
"ISO erstellt: $($item.FullName) ($([math]::Round($item.Length/1KB)) KB)"
