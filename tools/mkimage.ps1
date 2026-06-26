param(
    [Parameter(Mandatory = $true)][string]$Boot,
    [Parameter(Mandatory = $true)][string]$Kernel,
    [Parameter(Mandatory = $true)][string]$Out,
    [int]$KernelSectors = 64,
    [int]$FsStart = -1,
    [int]$FsSectors = 0,
    [switch]$ResetFs
)

$ErrorActionPreference = "Stop"

$bootBytes = [IO.File]::ReadAllBytes((Resolve-Path $Boot))
$kernelBytes = [IO.File]::ReadAllBytes((Resolve-Path $Kernel))
$maxKernelBytes = $KernelSectors * 512
$kernelEndSector = 1 + $KernelSectors
if ($FsStart -lt 0) {
    $FsStart = $kernelEndSector
}

if ($bootBytes.Length -ne 512) {
    throw "Boot sector must be exactly 512 bytes, got $($bootBytes.Length)."
}

if ($kernelBytes.Length -gt $maxKernelBytes) {
    throw "Kernel is $($kernelBytes.Length) bytes, but boot.asm loads only $maxKernelBytes bytes."
}

if ($FsSectors -gt 0 -and $FsStart -lt $kernelEndSector) {
    throw "Filesystem starts at LBA $FsStart, but kernel area ends at LBA $kernelEndSector."
}

$fsBytes = $null
if ($FsSectors -gt 0 -and -not $ResetFs -and (Test-Path $Out)) {
    $oldBytes = [IO.File]::ReadAllBytes((Resolve-Path $Out))
    $fsLength = $FsSectors * 512

    function Test-MinifsSuper([byte[]]$Bytes, [int]$Sector) {
        $offset = $Sector * 512
        if ($offset + 4 -gt $Bytes.Length) {
            return $false
        }
        return $Bytes[$offset] -eq 0x4D -and
            $Bytes[$offset + 1] -eq 0x42 -and
            $Bytes[$offset + 2] -eq 0x46 -and
            $Bytes[$offset + 3] -eq 0x53
    }

    function Find-MinifsStart([byte[]]$Bytes, [int]$PreferredSector, [int]$FsLength) {
        if ((Test-MinifsSuper $Bytes $PreferredSector) -and
            $Bytes.Length -ge (($PreferredSector * 512) + $FsLength)) {
            return $PreferredSector
        }
        $lastSector = [Math]::Floor(($Bytes.Length - 4) / 512)
        for ($sector = 1; $sector -le $lastSector; $sector++) {
            if ((Test-MinifsSuper $Bytes $sector) -and
                $Bytes.Length -ge (($sector * 512) + $FsLength)) {
                return $sector
            }
        }
        return -1
    }

    $oldFsStart = Find-MinifsStart $oldBytes $FsStart $fsLength
    if ($oldFsStart -ge 0) {
        $fsOffset = $oldFsStart * 512
        $fsBytes = [byte[]]::new($fsLength)
        [Array]::Copy($oldBytes, $fsOffset, $fsBytes, 0, $fsLength)
        $newDataLba = [uint32]($FsStart + 1 + 128 + 1)
        $dataLbaBytes = [BitConverter]::GetBytes($newDataLba)
        [Array]::Copy($dataLbaBytes, 0, $fsBytes, 12, 4)
        if ($oldFsStart -ne $FsStart) {
            Write-Host "Preserved /fs from old LBA $oldFsStart -> new LBA $FsStart"
        }
    }
}

$outDir = Split-Path $Out -Parent
if ($outDir) {
    New-Item -ItemType Directory -Force $outDir | Out-Null
}

$fs = $null
$fs = [IO.File]::Open($Out, [IO.FileMode]::Create, [IO.FileAccess]::Write)
try {
    $fs.Write($bootBytes, 0, $bootBytes.Length)
    $fs.Write($kernelBytes, 0, $kernelBytes.Length)

    $padding = $maxKernelBytes - $kernelBytes.Length
    if ($padding -gt 0) {
        $zeroes = [byte[]]::new($padding)
        $fs.Write($zeroes, 0, $zeroes.Length)
    }
    if ($FsSectors -gt 0) {
        $fsPadSectors = $FsStart - $kernelEndSector
        if ($fsPadSectors -gt 0) {
            $zeroes = [byte[]]::new($fsPadSectors * 512)
            $fs.Write($zeroes, 0, $zeroes.Length)
        }
        if ($fsBytes -ne $null) {
            $fs.Write($fsBytes, 0, $fsBytes.Length)
        } else {
            $zeroes = [byte[]]::new($FsSectors * 512)
            $fs.Write($zeroes, 0, $zeroes.Length)
        }
    }
}
finally {
    if ($fs -ne $null) {
        $fs.Dispose()
    }
}

$totalSectors = if ($FsSectors -gt 0) { $FsStart + $FsSectors } else { $kernelEndSector }
Write-Host "Wrote $Out ($($totalSectors * 512) bytes)"
