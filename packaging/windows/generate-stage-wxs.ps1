param(
    [Parameter(Mandatory = $true)]
    [string]$StageDir,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"

$stage = (Resolve-Path -LiteralPath $StageDir).ProviderPath
$app = Join-Path $stage "compactphone.exe"
if (-not (Test-Path -LiteralPath $app -PathType Leaf)) {
    throw "compactphone.exe not found in staged runtime: $stage"
}

$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName)
if ($files.Count -eq 0) {
    throw "No files found in staged runtime: $stage"
}

function Get-HashSuffix {
    param([Parameter(Mandatory = $true)][string]$Value)

    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $normalized = $Value.Replace('\', '/').ToLowerInvariant()
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($normalized)
        $hash = $sha.ComputeHash($bytes)
        return (-join ($hash | ForEach-Object { $_.ToString("x2") })).Substring(0, 12)
    } finally {
        $sha.Dispose()
    }
}

function ConvertTo-WixId {
    param(
        [Parameter(Mandatory = $true)][string]$Prefix,
        [Parameter(Mandatory = $true)][string]$Value
    )

    $name = [System.IO.Path]::GetFileNameWithoutExtension($Value)
    if ([string]::IsNullOrWhiteSpace($name)) {
        $name = "item"
    }

    $safe = ($name -replace '[^A-Za-z0-9_]', '_').Trim('_')
    if ([string]::IsNullOrWhiteSpace($safe)) {
        $safe = "item"
    }
    if ($safe.Length -gt 32) {
        $safe = $safe.Substring(0, 32)
    }
    if ($safe[0] -match '[0-9]') {
        $safe = "i_$safe"
    }

    return "{0}_{1}_{2}" -f $Prefix, $safe, (Get-HashSuffix $Value)
}

function Escape-XmlAttr {
    param([Parameter(Mandatory = $true)][string]$Value)
    return [System.Security.SecurityElement]::Escape($Value)
}

function New-Node {
    param(
        [string]$RelativePath,
        [string]$Name,
        [string]$Id
    )

    return @{
        RelativePath = $RelativePath
        Name = $Name
        Id = $Id
        Dirs = [ordered]@{}
        Files = New-Object System.Collections.ArrayList
    }
}

$root = New-Node -RelativePath "" -Name "" -Id "INSTALLFOLDER"
$componentRefs = [System.Collections.Generic.List[string]]::new()

foreach ($file in $files) {
    $relative = [System.IO.Path]::GetRelativePath($stage, $file.FullName).Replace('/', '\')
    $relativeParts = @($relative -split '[\\/]')
    $dirParts = @()
    if ($relativeParts.Count -gt 1) {
        $dirParts = $relativeParts[0..($relativeParts.Count - 2)]
    }
    $isAppExe = $relative -ieq "compactphone.exe"

    $node = $root
    $currentRelative = ""
    foreach ($part in $dirParts) {
        if ([string]::IsNullOrWhiteSpace($part)) {
            continue
        }

        $nextRelative = if ([string]::IsNullOrEmpty($currentRelative)) {
            $part
        } else {
            Join-Path $currentRelative $part
        }

        if (-not $node.Dirs.Contains($part)) {
            $node.Dirs[$part] = New-Node `
                -RelativePath $nextRelative `
                -Name $part `
                -Id (ConvertTo-WixId -Prefix "dir" -Value $nextRelative)
        }

        $node = $node.Dirs[$part]
        $currentRelative = $nextRelative
    }

    $componentId = if ($isAppExe) {
        "cmp_AppExe"
    } else {
        ConvertTo-WixId -Prefix "cmp" -Value $relative
    }
    $fileId = if ($isAppExe) {
        "AppExe"
    } else {
        ConvertTo-WixId -Prefix "fil" -Value $relative
    }

    [void]$node.Files.Add([pscustomobject]@{
        RelativePath = $relative
        ComponentId = $componentId
        FileId = $fileId
        Source = $file.FullName
    })
    $componentRefs.Add($componentId)
}

$lines = [System.Collections.Generic.List[string]]::new()
function Add-Line {
    param([string]$Text)
    [void]$script:lines.Add($Text)
}

function Write-Node {
    param(
        [hashtable]$Node,
        [int]$Level
    )

    $indent = "  " * $Level
    foreach ($entry in @($Node.Files | Sort-Object RelativePath)) {
        Add-Line ("{0}<Component Id=""{1}"" Guid=""*"">" -f $indent, (Escape-XmlAttr $entry.ComponentId))
        Add-Line ("{0}  <File Id=""{1}"" Source=""{2}"" KeyPath=""yes"" />" -f $indent, (Escape-XmlAttr $entry.FileId), (Escape-XmlAttr $entry.Source))
        Add-Line ("{0}</Component>" -f $indent)
    }

    foreach ($name in @($Node.Dirs.Keys | Sort-Object)) {
        $child = $Node.Dirs[$name]
        Add-Line ("{0}<Directory Id=""{1}"" Name=""{2}"">" -f $indent, (Escape-XmlAttr $child.Id), (Escape-XmlAttr $child.Name))
        Write-Node -Node $child -Level ($Level + 1)
        Add-Line ("{0}</Directory>" -f $indent)
    }
}

Add-Line '<?xml version="1.0" encoding="UTF-8"?>'
Add-Line '<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">'
Add-Line '  <Fragment>'
Add-Line '    <DirectoryRef Id="INSTALLFOLDER">'
Write-Node -Node $root -Level 3
Add-Line '    </DirectoryRef>'
Add-Line '  </Fragment>'
Add-Line '  <Fragment>'
Add-Line '    <ComponentGroup Id="MainComponents">'
foreach ($componentId in @($componentRefs | Sort-Object)) {
    Add-Line ("      <ComponentRef Id=""{0}"" />" -f (Escape-XmlAttr $componentId))
}
Add-Line '    </ComponentGroup>'
Add-Line '  </Fragment>'
Add-Line '</Wix>'

$outputParent = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($outputParent)) {
    New-Item -ItemType Directory -Path $outputParent -Force | Out-Null
}
Set-Content -LiteralPath $OutputPath -Value $lines -Encoding UTF8
Write-Host "Generated $OutputPath with $($files.Count) staged files."
