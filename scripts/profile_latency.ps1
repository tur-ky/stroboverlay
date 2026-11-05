param(
    [int]$DurationSeconds = 0
)

# PowerShell script to profile latency while running stroboverlay.exe

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exePath = Join-Path $root "bin\Release\stroboverlay.exe"
$outputFile = Join-Path $root "latency_profile.csv"

if (-not (Test-Path $exePath)) {
    Write-Error "Executable not found at $exePath. Please build first."
    exit 1
}

# Start the overlay process
$process = Start-Process -FilePath $exePath -PassThru
$writer = $null

# Function to collect performance counters
function Collect-Counters {
    $cpuCounter = $null
    $csCounter = $null
    $qlCounter = $null

    try {
        $cpuCounter = [System.Diagnostics.PerformanceCounter]::new("Processor", "% Processor Time", "_Total")
        $csCounter = [System.Diagnostics.PerformanceCounter]::new("System", "Context Switches/sec")
        $qlCounter = [System.Diagnostics.PerformanceCounter]::new("System", "Processor Queue Length")

        $null = $cpuCounter.NextValue()
        $null = $csCounter.NextValue()
        $null = $qlCounter.NextValue()
    } catch {
        Write-Warning "Failed to initialize system performance counters: $_"
    }

    function Get-GpuCounters {
        $counters = @()
        try {
            if ([System.Diagnostics.PerformanceCounterCategory]::Exists("GPU Engine")) {
                $gpuCat = [System.Diagnostics.PerformanceCounterCategory]::new("GPU Engine")
                $targetPid = $process.Id
                foreach ($inst in $gpuCat.GetInstanceNames()) {
                    if ($inst -like "pid_${targetPid}_*") {
                        $gc = [System.Diagnostics.PerformanceCounter]::new("GPU Engine", "Utilization Percentage", $inst)
                        $null = $gc.NextValue()
                        $counters += $gc
                    }
                }
            }
        } catch {}
        return ,$counters
    }

    $gpuCounters = @(Get-GpuCounters)
    $lastGpuCheckMs = 0

    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

    try {
        while ($process.HasExited -eq $false) {
            if ($DurationSeconds -gt 0 -and $stopwatch.Elapsed.TotalSeconds -ge $DurationSeconds) {
                break
            }

            if (($null -eq $gpuCounters -or $gpuCounters.Count -eq 0) -and ($stopwatch.ElapsedMilliseconds - $lastGpuCheckMs -ge 1000)) {
                $gpuCounters = @(Get-GpuCounters)
                $lastGpuCheckMs = $stopwatch.ElapsedMilliseconds
            }

            # Get the current timestamp
            $timestamp = Get-Date -Format "o"

            $cpuUsage = 0
            $contextSwitches = 0
            $queueLength = 0
            $gpuUsage = 0

            try {
                if ($cpuCounter) { $cpuUsage = $cpuCounter.NextValue() }
                if ($csCounter) { $contextSwitches = $csCounter.NextValue() }
                if ($qlCounter) { $queueLength = $qlCounter.NextValue() }
            } catch {}

            if ($gpuCounters.Count -gt 0) {
                $gpuSum = 0
                foreach ($gc in $gpuCounters) {
                    try {
                        $gpuSum += $gc.NextValue()
                    } catch {}
                }
                $gpuUsage = $gpuSum
            }

            # Write the data to the CSV file
            $writer.WriteLine("$timestamp,$cpuUsage,$gpuUsage,$contextSwitches,$queueLength")

            # Wait for 10 ms before the next sample
            Start-Sleep -Milliseconds 10
        }
    } finally {
        if ($cpuCounter) { $cpuCounter.Dispose() }
        if ($csCounter) { $csCounter.Dispose() }
        if ($qlCounter) { $qlCounter.Dispose() }
        foreach ($gc in $gpuCounters) {
            try { $gc.Dispose() } catch {}
        }
    }
}

try {
    # Initialize the CSV file with headers
    $writer = [System.IO.StreamWriter]::new($outputFile, $false, [System.Text.Encoding]::UTF8)
    $writer.WriteLine("Timestamp,CPU_Usage,GPU_Usage,Context_Switches,Queue_Length")

    # Start collecting counters
    Collect-Counters
}
finally {
    # Clean up
    if ($writer) {
        $writer.Dispose()
    }
    if ($process -and -not $process.HasExited) {
        $null = $process.CloseMainWindow()
        $null = $process.WaitForExit(2000)
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
}
