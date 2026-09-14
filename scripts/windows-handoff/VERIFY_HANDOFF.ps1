# Read-only SHA-256 verification; compatible with Windows PowerShell 5.1.
# Run from the extracted handoff root before changing source files.
$ErrorActionPreference = 'Stop'
$handoffRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$handoffPrefix = $handoffRoot + [IO.Path]::DirectorySeparatorChar
$manifestPath = Join-Path $handoffRoot 'SHA256SUMS.txt'
$verifiedCount = 0
$seenPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)

try {
    foreach ($line in Get-Content -LiteralPath $manifestPath -Encoding UTF8) {
        if ($line -notmatch '^([0-9a-f]{64})  (.+)$') {
            throw "Malformed checksum line: $line"
        }
        $expectedHash = $Matches[1]
        $relativePath = $Matches[2]
        if ([IO.Path]::IsPathRooted($relativePath) -or
            $relativePath.Contains('\') -or $relativePath.Contains(':') -or
            (($relativePath.Split('/')) | Where-Object { $_ -eq '..' -or $_ -eq '.' -or $_ -eq '' })) {
            throw "Unsafe manifest path: $relativePath"
        }
        if (-not $seenPaths.Add($relativePath)) {
            throw "Duplicate manifest path: $relativePath"
        }
        $fullPath = [IO.Path]::GetFullPath((Join-Path $handoffRoot $relativePath))
        if (-not $fullPath.StartsWith($handoffPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Path escaped handoff folder: $relativePath"
        }
        # Reject junctions/symlinks in the file or any intermediate directory.
        $checkedPath = $fullPath
        while ($checkedPath -ne $handoffRoot) {
            $item = Get-Item -LiteralPath $checkedPath -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Unexpected reparse point: $relativePath"
            }
            $checkedPath = [IO.Path]::GetDirectoryName($checkedPath)
        }
        $actualHash = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash
        if ($actualHash -ine $expectedHash) {
            throw "SHA-256 mismatch: $relativePath"
        }
        $verifiedCount++
    }
    if ($verifiedCount -eq 0) { throw 'Empty checksum manifest' }
    Write-Host "PASS: $verifiedCount payload files match SHA256SUMS.txt."
    Write-Host 'This checks transfer integrity, not Windows/REAPER acceptance.'
    exit 0
} catch {
    Write-Error "Handoff verification failed: $_"
    exit 1
}
