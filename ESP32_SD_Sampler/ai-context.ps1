[CmdletBinding()]
param(
    [string]$Output = ".ai-context",

    [ValidateRange(1, 1048576)]
    [int]$ChunkKB = 1500,

    [ValidateRange(1, 1048576)]
    [int]$MaxFileKB = 750,

    [switch]$IncludeAllText,

    [switch]$IncludeEmpty,

    [switch]$NoCombined
)

$ErrorActionPreference = "Stop"

# $PSScriptRoot preserves the actual UNC path:
# \\server\share\project
$ProjectRoot = $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    throw "Cannot determine the project directory."
}

$NodeCommand = Get-Command node -ErrorAction SilentlyContinue

if ($null -eq $NodeCommand) {
    throw "Node.js was not found in PATH."
}

$ScriptPath = Join-Path $ProjectRoot "build-ai-context.js"

if (-not (Test-Path -LiteralPath $ScriptPath -PathType Leaf)) {
    throw "build-ai-context.js was not found: $ScriptPath"
}

Write-Host "Building AI project context..."
Write-Host "Project directory: $ProjectRoot"
Write-Host ""

$NodeArguments = @(
    $ScriptPath
    "--root"
    $ProjectRoot
    "--output"
    $Output
    "--chunk-kb"
    $ChunkKB
    "--max-file-kb"
    $MaxFileKB
)

if ($IncludeAllText) {
    $NodeArguments += "--include-all-text"
}

if ($IncludeEmpty) {
    $NodeArguments += "--include-empty"
}

if ($NoCombined) {
    $NodeArguments += "--no-combined"
}

& $NodeCommand.Source @NodeArguments

$ExitCode = $LASTEXITCODE

if ($ExitCode -ne 0) {
    throw "Context generation failed with exit code $ExitCode."
}

Write-Host ""
Write-Host "Done."
Write-Host "Output: $(Join-Path $ProjectRoot $Output)"