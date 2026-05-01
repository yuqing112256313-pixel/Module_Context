param()

$ErrorActionPreference = 'Continue'

$processes = @(Get-Process -Name 'mc_task_flow_worker_host' -ErrorAction SilentlyContinue)
if ($processes.Count -eq 0) {
    Write-Host '[worker-stop] no running worker processes found'
    return
}

foreach ($process in $processes) {
    try {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        Wait-Process -Id $process.Id -Timeout 10 -ErrorAction SilentlyContinue
    } catch {
    } finally {
        try {
            $process.Dispose()
        } catch {
        }
    }
}
Write-Host "[worker-stop] stopped worker process count: $($processes.Count)"
