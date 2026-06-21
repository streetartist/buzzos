param(
    [Parameter(Mandatory = $true)][string]$Boot,
    [Parameter(Mandatory = $true)][string]$Kernel,
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$KernelSectors = 64
)

$bootBytes = [IO.File]::ReadAllBytes((Resolve-Path $Boot))
$kernelBytes = [IO.File]::ReadAllBytes((Resolve-Path $Kernel))
$maxKernelBytes = $KernelSectors * 512

if ($bootBytes.Length -ne 512) {
    throw "Boot sector must be exactly 512 bytes, got $($bootBytes.Length)."
}

if ($kernelBytes.Length -gt $maxKernelBytes) {
    throw "Kernel is $($kernelBytes.Length) bytes, but boot.asm loads only $maxKernelBytes bytes."
}

$outDir = Split-Path $Out -Parent
if ($outDir) {
    New-Item -ItemType Directory -Force $outDir | Out-Null
}

$fs = [IO.File]::Open($Out, [IO.FileMode]::Create, [IO.FileAccess]::Write)
try {
    $fs.Write($bootBytes, 0, $bootBytes.Length)
    $fs.Write($kernelBytes, 0, $kernelBytes.Length)

    $padding = $maxKernelBytes - $kernelBytes.Length
    if ($padding -gt 0) {
        $zeroes = [byte[]]::new($padding)
        $fs.Write($zeroes, 0, $zeroes.Length)
    }
}
finally {
    $fs.Dispose()
}

Write-Host "Wrote $Out ($($bootBytes.Length + $maxKernelBytes) bytes)"

