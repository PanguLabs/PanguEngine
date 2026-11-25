function Get-ActivityPresentation([string]$activity) {
    $layerPrefix = [regex]::Escape($activity)
    $layers = @(foreach ($entry in (Invoke-Device shell dumpsys SurfaceFlinger --list)) {
        if ($entry -match "^RequestedLayerState\{($layerPrefix#\d+) parentId=") {
            $Matches[1]
        } elseif ($entry.Trim() -match "^$layerPrefix(?:#\d+)?$") {
            $entry.Trim()
        }
    })
    if ($layers.Count -ne 1) { return }

    $latency = @(Invoke-Device shell ('dumpsys SurfaceFlinger --latency "' + $layers[0] + '"'))
    $refreshNs = 0L
    if ($latency.Count -eq 0 -or ![long]::TryParse($latency[0], [ref]$refreshNs)) { return }

    $presentTimes = @(foreach ($entry in $latency) {
        if ($entry -match '^\s*\d+\s+(\d+)\s+\d+\s*$') {
            $timestamp = [long]$Matches[1]
            if ($timestamp -gt 0 -and $timestamp -lt [long]::MaxValue) { $timestamp }
        }
    })
    [pscustomobject]@{
        Layer = $layers[0]
        RefreshMs = $refreshNs / 1e6
        PresentTimes = $presentTimes
        Latency = $latency
    }
}
