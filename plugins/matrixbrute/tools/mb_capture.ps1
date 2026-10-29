# Records the USB traffic between MIDI Control Center and the MatrixBrute on Windows, for
# mb_usb_extract.py. Needs Wireshark with USBPcap (see ../docs/capture.md). Run from PowerShell:
#
#    powershell -ExecutionPolicy Bypass -File mb_capture.ps1 -Seconds 120 -Out capture.pcapng
#
# It captures every USBPcap root hub at once (the MatrixBrute's hub isn't known ahead of time), for
# -Seconds seconds, into one pcapng file. Do the MCC steps while it runs.

param(
   [int]$Seconds = 120,
   [string]$Out = "matrixbrute-capture.pcapng"
)

$ErrorActionPreference = "Stop"

$dumpcap = @(
   "$env:ProgramFiles\Wireshark\dumpcap.exe",
   "${env:ProgramFiles(x86)}\Wireshark\dumpcap.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $dumpcap) {
   Write-Error "dumpcap.exe not found. Install Wireshark (winget install WiresharkFoundation.Wireshark)."
}

# dumpcap -D lists interfaces as "N. name (description)"; USBPcap ones are named \\.\USBPcapN.
$interfaces = & $dumpcap -D 2>$null | ForEach-Object {
   if ($_ -match '^\d+\.\s+(\\\\\.\\USBPcap\d+)') { $Matches[1] }
}
if (-not $interfaces) {
   Write-Error ("No USBPcap interfaces. Install USBPcap (it's an option in the Wireshark installer, or " +
                "https://desowin.org/usbpcap/), then reboot so its driver loads.")
}

Write-Host "Capturing $($interfaces -join ', ') for $Seconds s into $Out"
Write-Host "Now, in MIDI Control Center: connect to the MatrixBrute and sync / receive its presets."
$dumpArgs = @()
foreach ($i in $interfaces) { $dumpArgs += @("-i", $i) }
& $dumpcap @dumpArgs -a "duration:$Seconds" -w $Out
Write-Host "Done. Next: python mb_usb_extract.py $Out out"
