param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("linux-x64", "linux-arm64", "windows-x64", "macos-arm64")]
    [string]$Rid
)

$ErrorActionPreference = "Stop"
foreach ($name in @("GITHUB_TOKEN", "RUNNER_TEMP", "GITHUB_ENV")) {
    if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($name))) {
        throw "$name is required"
    }
}

# Resolve on every run. Cache payloads, never project.assets.json or a lock file.
$restoreRoot = Join-Path $env:RUNNER_TEMP "rulesforge-sdk-restore"
New-Item -ItemType Directory -Force -Path $restoreRoot | Out-Null
$project = Join-Path $restoreRoot "native-sdks.csproj"
$packages = Join-Path $env:RUNNER_TEMP "rulesforge-nuget"
$config = Join-Path $PSScriptRoot "../vcpkg-cache.nuget.config"
@'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <RestorePackagesWithLockFile>false</RestorePackagesWithLockFile>
  </PropertyGroup>
  <ItemGroup>
    <PackageReference Include="Salts.Native" Version="*-*" />
    <PackageReference Include="SaltsUtils.Native" Version="*-*" />
  </ItemGroup>
</Project>
'@ | Set-Content -LiteralPath $project -Encoding utf8NoBOM

& dotnet restore $project --packages $packages --configfile $config --no-cache --force-evaluate
if ($LASTEXITCODE -ne 0) { throw "Native SDK restoration failed" }
$assets = Get-Content (Join-Path $restoreRoot "obj/project.assets.json") -Raw | ConvertFrom-Json
foreach ($package in @(
    @{ Id = "Salts.Native"; Root = "SALTS_ROOT"; Manifest = "salts-sdk-manifest.txt" },
    @{ Id = "SaltsUtils.Native"; Root = "SALTS_UTILS_ROOT"; Manifest = "salts-utils-sdk-manifest.txt" }
)) {
    $identity = @($assets.libraries.PSObject.Properties.Name | Where-Object {
        $_.StartsWith("$($package.Id)/", [StringComparison]::OrdinalIgnoreCase)
    })
    if ($identity.Count -ne 1) { throw "Expected one resolved $($package.Id) package" }
    $root = (Join-Path $packages "$($identity[0].ToLowerInvariant())/sdk/$Rid").Replace('\', '/')
    $manifest = Join-Path $root $package.Manifest
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { throw "Missing SDK manifest: $manifest" }
    "$($package.Root)=$root" >> $env:GITHUB_ENV
    Write-Host "Resolved $($identity[0]) for $Rid"
    Get-Content -LiteralPath $manifest
    if ($env:GITHUB_STEP_SUMMARY) {
        "### $($identity[0]) / $Rid" >> $env:GITHUB_STEP_SUMMARY
        Get-Content -LiteralPath $manifest >> $env:GITHUB_STEP_SUMMARY
    }
}

# Normalize action-provided Windows paths at their environment boundary.
foreach ($name in @("VCPKG_ROOT", "VCPKG_CACHE_REPOSITORY_ROOT", "RE2C_ROOT")) {
    $value = [Environment]::GetEnvironmentVariable($name)
    if ([string]::IsNullOrWhiteSpace($value)) { throw "$name is required" }
    "$name=$($value.Replace('\', '/'))" >> $env:GITHUB_ENV
}
