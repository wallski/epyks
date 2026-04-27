$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path "rnnoise" | Out-Null
$base = "https://raw.githubusercontent.com/xiph/rnnoise/master/src/"
$files = @(
    "rnnoise.h", "rnn.c", "rnn.h", "rnn_data.c", "rnn_data.h",
    "denoise.c", "pitch.c", "pitch.h",
    "kiss_fft.c", "kiss_fft.h", "_kiss_fft_guts.h",
    "common.h", "arch.h", "tansig_table.h"
)
foreach ($f in $files) {
    $url = $base + $f
    $out = "rnnoise\" + $f
    try {
        Invoke-WebRequest -Uri $url -OutFile $out
        Write-Host "OK: $f"
    } catch {
        Write-Host "FAIL: $f - $($_.Exception.Message)"
    }
}
Write-Host "Done."
