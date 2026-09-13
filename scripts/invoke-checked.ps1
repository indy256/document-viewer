function Invoke-Checked {
    param([string]$Program, [Parameter(ValueFromRemainingArguments=$true)][string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE) { throw "$Program failed with exit code $LASTEXITCODE" }
}
