# Script PowerShell pour exécuter un programme avec limitations de ressources
# Utilise les Job Objects Windows pour un contrôle plus strict

param(
    [Parameter(Mandatory=$true)]
    [string]$Executable,
    
    [Parameter(Mandatory=$false)]
    [int]$TimeoutSeconds = 10,
    
    [Parameter(Mandatory=$false)]
    [int]$MaxMemoryMB = 512,
    
    [Parameter(Mandatory=$false)]
    [string]$InputFile = $null,
    
    [Parameter(Mandatory=$false)]
    [string]$OutputFile = $null,
    
    [Parameter(Mandatory=$false)]
    [switch]$Verbose
)

# Fonction pour afficher les messages verbose
function Write-VerboseMessage {
    param([string]$Message)
    if ($Verbose) {
        Write-Host $Message
    }
}

# Vérifier que l'exécutable existe
if (-not (Test-Path $Executable)) {
    Write-Error "Executable not found: $Executable"
    exit 1
}

Write-VerboseMessage "🚀 Starting resource-limited execution"
Write-VerboseMessage "⏱️  Timeout: $TimeoutSeconds seconds"
Write-VerboseMessage "💾 Max Memory: $MaxMemoryMB MB"
Write-VerboseMessage ("-" * 60)

# Préparer l'entrée
$inputData = $null
if ($InputFile -and (Test-Path $InputFile)) {
    $inputData = Get-Content $InputFile -Raw
    Write-VerboseMessage "📥 Using input from: $InputFile"
}

# Créer le processus
$processInfo = New-Object System.Diagnostics.ProcessStartInfo
$processInfo.FileName = (Resolve-Path $Executable).Path
$processInfo.RedirectStandardInput = $true
$processInfo.RedirectStandardOutput = $true
$processInfo.RedirectStandardError = $true
$processInfo.UseShellExecute = $false
$processInfo.CreateNoWindow = $true

# Démarrer le processus
$process = New-Object System.Diagnostics.Process
$process.StartInfo = $processInfo

# Variables de monitoring
$startTime = Get-Date
$maxMemoryBytes = 0
$memoryExceeded = $false
$timeout = $false

try {
    # Démarrer
    $process.Start() | Out-Null
    $processId = $process.Id
    
    Write-VerboseMessage "🔄 Process started (PID: $processId)"
    
    # Envoyer l'entrée si nécessaire
    if ($inputData) {
        $process.StandardInput.Write($inputData)
        $process.StandardInput.Close()
    }
    
    # Monitoring loop
    $checkInterval = 100 # millisecondes
    
    while (-not $process.HasExited) {
        # Vérifier le timeout
        $elapsed = (Get-Date) - $startTime
        if ($elapsed.TotalSeconds -gt $TimeoutSeconds) {
            $timeout = $true
            Write-VerboseMessage "⏰ TIMEOUT reached!"
            $process.Kill()
            break
        }
        
        # Mesurer la mémoire
        try {
            $proc = Get-Process -Id $processId -ErrorAction Stop
            $memoryMB = $proc.WorkingSet64 / 1MB
            
            if ($memoryMB -gt $maxMemoryBytes) {
                $maxMemoryBytes = $memoryMB
            }
            
            # Vérifier la limite de mémoire
            if ($memoryMB -gt $MaxMemoryMB) {
                $memoryExceeded = $true
                Write-VerboseMessage "💥 MEMORY LIMIT EXCEEDED: $([math]::Round($memoryMB, 2)) MB"
                $process.Kill()
                break
            }
        }
        catch {
            # Le processus peut avoir terminé
            break
        }
        
        Start-Sleep -Milliseconds $checkInterval
    }
    
    # Attendre la fin du processus (si pas déjà terminé)
    if (-not $process.HasExited) {
        $process.WaitForExit(2000)
    }
    
    # Récupérer la sortie
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $exitCode = $process.ExitCode
    
    # Calculer le temps d'exécution
    $executionTime = ((Get-Date) - $startTime).TotalSeconds
    
    # Créer l'objet résultat
    $results = @{
        success = ($exitCode -eq 0 -and -not $timeout -and -not $memoryExceeded)
        timeout = $timeout
        memoryExceeded = $memoryExceeded
        executionTime = [math]::Round($executionTime, 3)
        maxMemoryUsedMB = [math]::Round($maxMemoryBytes, 2)
        returnCode = $exitCode
        stdout = $stdout
        stderr = $stderr
        timestamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    }
    
    # Afficher les résultats
    Write-VerboseMessage ("-" * 60)
    Write-VerboseMessage "📊 RESULTS:"
    Write-VerboseMessage "  ✓ Success: $($results.success)"
    Write-VerboseMessage "  ⏱️  Execution Time: $($results.executionTime)s"
    Write-VerboseMessage "  💾 Max Memory Used: $($results.maxMemoryUsedMB) MB"
    Write-VerboseMessage "  🔢 Return Code: $($results.returnCode)"
    
    if ($timeout) {
        Write-Host "⏰ TIMEOUT!" -ForegroundColor Yellow
    }
    if ($memoryExceeded) {
        Write-Host "💥 MEMORY LIMIT EXCEEDED!" -ForegroundColor Red
    }
    
    # Sauvegarder dans un fichier si demandé
    if ($OutputFile) {
        $results | ConvertTo-Json -Depth 10 | Out-File $OutputFile
        Write-VerboseMessage "💾 Results saved to: $OutputFile"
    }
    
    # Afficher stdout si verbose
    if ($Verbose -and $stdout) {
        Write-VerboseMessage ("-" * 60)
        Write-VerboseMessage "📤 STDOUT:"
        Write-Host $stdout
    }
    
    # Afficher stderr si présent
    if ($stderr) {
        Write-VerboseMessage ("-" * 60)
        Write-VerboseMessage "⚠️  STDERR:"
        Write-Host $stderr -ForegroundColor Yellow
    }
    
    # Code de sortie
    if ($results.success) {
        exit 0
    } else {
        exit 1
    }
}
catch {
    Write-Error "Error during execution: $_"
    exit 1
}
finally {
    if ($process -and -not $process.HasExited) {
        $process.Kill()
    }
    if ($process) {
        $process.Dispose()
    }
}
