[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = Join-Path $repositoryRoot "src"
$projectPath = Join-Path $repositoryRoot "Solace.vcxproj"
$filtersPath = Join-Path $repositoryRoot "Solace.vcxproj.filters"
$failures = [System.Collections.Generic.List[string]]::new()
$sourceExtensions = ".c", ".cpp", ".h", ".hpp", ".inl"

function Get-NormalizedSourceItems {
    param(
        [xml] $Document,
        [System.Xml.XmlNamespaceManager] $NamespaceManager
    )

    return @($Document.SelectNodes("//m:ClCompile[@Include] | //m:ClInclude[@Include]", $NamespaceManager)) |
        ForEach-Object { $_.Include.Replace("/", "\") } |
        Where-Object { $_ -like "src\*" }
}

function Add-ComparisonFailures {
    param(
        [string[]] $Expected,
        [string[]] $Actual,
        [string] $ManifestName
    )

    foreach ($difference in @(Compare-Object $Expected $Actual)) {
        $description = if ($difference.SideIndicator -eq "=>") {
            "is listed but missing from disk"
        }
        else {
            "exists on disk but is not listed"
        }
        $failures.Add(("{0}: {1} {2}." -f $ManifestName, $difference.InputObject, $description))
    }
}

[xml] $project = Get-Content -LiteralPath $projectPath
[xml] $filters = Get-Content -LiteralPath $filtersPath

$projectNamespace = [System.Xml.XmlNamespaceManager]::new($project.NameTable)
$projectNamespace.AddNamespace("m", "http://schemas.microsoft.com/developer/msbuild/2003")
$filtersNamespace = [System.Xml.XmlNamespaceManager]::new($filters.NameTable)
$filtersNamespace.AddNamespace("m", "http://schemas.microsoft.com/developer/msbuild/2003")

$diskItems = Get-ChildItem -LiteralPath $sourceRoot -Recurse -File |
    Where-Object { $_.Extension -in $sourceExtensions } |
    ForEach-Object {
        $_.FullName.Substring($repositoryRoot.Length + 1).Replace("/", "\")
    } |
    Sort-Object -Unique

$projectItems = @(Get-NormalizedSourceItems $project $projectNamespace)
$filterItems = @(Get-NormalizedSourceItems $filters $filtersNamespace)

Add-ComparisonFailures $diskItems ($projectItems | Sort-Object -Unique) "Solace.vcxproj"
Add-ComparisonFailures $diskItems ($filterItems | Sort-Object -Unique) "Solace.vcxproj.filters"

foreach ($duplicate in @($projectItems | Group-Object | Where-Object Count -gt 1)) {
    $failures.Add("Solace.vcxproj: $($duplicate.Name) is listed $($duplicate.Count) times.")
}
foreach ($duplicate in @($filterItems | Group-Object | Where-Object Count -gt 1)) {
    $failures.Add("Solace.vcxproj.filters: $($duplicate.Name) is listed $($duplicate.Count) times.")
}

$definedFilters = @($filters.SelectNodes("//m:Filter[@Include]", $filtersNamespace) |
    ForEach-Object { $_.Include })
$usedFilters = @($filters.SelectNodes("//m:ClCompile/m:Filter | //m:ClInclude/m:Filter", $filtersNamespace) |
    ForEach-Object { $_.InnerText } |
    Sort-Object -Unique)
foreach ($undefinedFilter in @($usedFilters | Where-Object { $_ -notin $definedFilters })) {
    $failures.Add("Solace.vcxproj.filters: filter '$undefinedFilter' is used but not defined.")
}

$filteredSourceNodes = @($filters.SelectNodes(
    "//m:ClCompile[@Include] | //m:ClInclude[@Include]", $filtersNamespace)) |
    Where-Object { $_.Include -like "src\*" }
foreach ($sourceNode in $filteredSourceNodes) {
    if (-not $sourceNode.SelectSingleNode("m:Filter", $filtersNamespace)) {
        $failures.Add("Solace.vcxproj.filters: $($sourceNode.Include) has no filter assignment.")
    }
}

$includeRoots = @(
    $sourceRoot,
    (Join-Path $repositoryRoot "thirdparty\imgui"),
    (Join-Path $repositoryRoot "thirdparty\imgui\backends"),
    (Join-Path $repositoryRoot "thirdparty\imgui\misc\freetype"),
    (Join-Path $repositoryRoot "thirdparty\freetype\include")
)

$sourceFiles = Get-ChildItem -LiteralPath $sourceRoot -Recurse -File |
    Where-Object { $_.Extension -in $sourceExtensions }
foreach ($file in $sourceFiles) {
    $lineNumber = 0
    foreach ($line in Get-Content -LiteralPath $file.FullName) {
        $lineNumber++
        if ($line -notmatch '^\s*#\s*include\s*"([^"]+)"') {
            continue
        }

        $include = $Matches[1]
        $includePath = $include.Replace("/", [IO.Path]::DirectorySeparatorChar)
        $candidates = @((Join-Path $file.DirectoryName $includePath))
        $candidates += $includeRoots | ForEach-Object { Join-Path $_ $includePath }
        if (-not ($candidates | Where-Object { Test-Path -LiteralPath $_ })) {
            $relativeFile = $file.FullName.Substring($repositoryRoot.Length + 1)
            $failures.Add(("{0}:{1}: quoted include '{2}' does not resolve." -f $relativeFile, $lineNumber, $include))
        }
    }
}

if ($failures.Count -gt 0) {
    $message = "Source layout verification failed:`n - " + ($failures -join "`n - ")
    throw $message
}

Write-Host "Source layout verified: $($diskItems.Count) files, synchronized project filters, and resolved includes."
