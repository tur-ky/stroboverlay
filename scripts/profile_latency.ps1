# PowerShell script to profile latency while running stroboverlay.exe

# Define the path to the executable
$exePath = "S:\documents\stroboverlay\x64\Release\stroboverlay.exe"

# Define the output CSV file
$outputFile = "S:\documents\stroboverlay\latency_profile.csv"

# Initialize the CSV file with headers
"Timestamp,CPU_Usage,GPU_Usage,Context_Switches,Queue_Length" | Out-File -FilePath $outputFile -Encoding utf8

# Start the overlay process
$process = Start-Process -FilePath $exePath -PassThru

# Function to collect performance counters
function Collect-Counters {
    while ($process.HasExited -eq $false) {
        # Get the current timestamp
        $timestamp = Get-Date -Format "o"

        # Collect performance counter data
        $cpuUsage = Get-Counter '\Processor(_Total)\% Processor Time' | Select-Object -ExpandProperty CounterSamples | Select-Object -ExpandProperty CookedValue
        $gpuUsage = Get-Counter '\GPU Engine(*)\Utilization Percentage' | Select-Object -ExpandProperty CounterSamples | Measure-Object -Property CookedValue -Average | Select-Object -ExpandProperty Average
        $contextSwitches = Get-Counter '\System\Context Switches/sec' | Select-Object -ExpandProperty CounterSamples | Select-Object -ExpandProperty CookedValue
        $queueLength = Get-Counter '\System\Processor Queue Length' | Select-Object -ExpandProperty CounterSamples | Select-Object -ExpandProperty CookedValue

        # Write the data to the CSV file
        "$timestamp,$cpuUsage,$gpuUsage,$contextSwitches,$queueLength" | Out-File -FilePath $outputFile -Append -Encoding utf8

        # Wait for 10 ms before the next sample
        Start-Sleep -Milliseconds 10
    }
}

# Start collecting counters
Collect-Counters

# Wait for the user to close the overlay or press Ctrl-C
$process.WaitForExit()

# Clean up
Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
