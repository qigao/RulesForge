param(
  [Parameter(Mandatory = $true)]
  [ValidateSet("linux-x64", "windows-x64")]
  [string]$Rid
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
foreach ($name in @("RUNNER_TEMP", "GITHUB_ENV", "GITHUB_PATH")) {
  if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($name))) {
    throw "$name is required for curl-only SDK restoration"
  }
}
$token = if ($env:GH_TOKEN) { $env:GH_TOKEN } else { $env:GITHUB_TOKEN }
if ([string]::IsNullOrWhiteSpace($token)) {
  throw "GitHub Packages read token is required"
}
$env:GH_TOKEN = $token

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../.."))
$client = Join-Path $repositoryRoot "build/vcpkg-cache-contract/scripts/github-packages.py"
if (-not (Test-Path -LiteralPath $client -PathType Leaf)) {
  throw "The immutable curl GitHub Packages client checkout is missing: $client"
}
$root = Join-Path $env:RUNNER_TEMP "rulesforge-curl-native-sdk"
New-Item -ItemType Directory -Force -Path $root | Out-Null
$packages = Join-Path $root "packages"
New-Item -ItemType Directory -Force -Path $packages | Out-Null

function Restore-VerifiedNativePackage([string]$Name) {
  $archive = Join-Path $root "$Name.nupkg"
  $versionFile = Join-Path $root "$Name.version"
  # Original NuGet PackageReference Version="*" selects the latest stable
  # published package. Preserve that exact selector, but fetch via curl.
  & python $client latest $Name $archive $versionFile | Out-Host
  if ($LASTEXITCODE -ne 0) { throw "curl GitHub Packages failed for $Name" }
  if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) {
    throw "No resolved GitHub Packages version for $Name"
  }
  $version = (Get-Content -LiteralPath $versionFile -Raw).Trim()
  if ($version -notmatch '^[0-9]+([.][0-9]+){2,3}$') {
    throw "Nonstable or malformed resolved version for $Name : $version"
  }
  $id = $Name.ToLowerInvariant()
  $destination = Join-Path $packages "$id/$version"
  if (Test-Path -LiteralPath $destination) {
    Remove-Item -LiteralPath $destination -Recurse -Force
  }
  & python $client extract $archive $destination | Out-Host
  if ($LASTEXITCODE -ne 0) { throw "verified GitHub Package failed extraction: $Name" }
  return @{ Version = $version; Root = (Join-Path $destination "sdk/$Rid") }
}
$salts = Restore-VerifiedNativePackage "Salts.Native"
$utils = Restore-VerifiedNativePackage "SaltsUtils.Native"
$saltsRoot = $salts.Root
$saltsUtilsRoot = $utils.Root

foreach ($path in @(
  (Join-Path $saltsRoot "lib/cmake/Salts/SaltsConfig.cmake"),
  (Join-Path $saltsRoot "include/cmeta/interface.h"),
  (Join-Path $saltsUtilsRoot "lib/cmake/SaltsUtils/SaltsUtilsConfig.cmake"),
  (Join-Path $saltsUtilsRoot "include/data_bind.h"),
  (Join-Path $saltsUtilsRoot "include/data_bind_binding_plan.h")
)) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Downloaded published SDK is missing file: $path"
  }
}
$idlc = Join-Path $saltsUtilsRoot $(if ($Rid -eq "windows-x64") { "bin/salts-idlc.exe" } else { "bin/salts-idlc" })
if (-not (Test-Path -LiteralPath $idlc -PathType Leaf)) {
  throw "Published SaltsUtils SDK is missing salts-idlc"
}
$dataBindHeader = Get-Content -LiteralPath (Join-Path $saltsUtilsRoot "include/data_bind.h") -Raw
if ($dataBindHeader -notmatch '#define\s+DATA_BIND_VERSION_MAJOR\s+3' -or
    $dataBindHeader -notmatch '#define\s+DATA_BIND_ABI_VERSION\s+10') {
  throw "Verified SaltsUtils GitHub package lacks DataBind 3 / ABI 10"
}

"SALTS_ROOT=$saltsRoot" >> $env:GITHUB_ENV
"SALTS_UTILS_ROOT=$saltsUtilsRoot" >> $env:GITHUB_ENV
"SALTS_PACKAGE_VERSION=$($salts.Version)" >> $env:GITHUB_ENV
"SALTS_UTILS_PACKAGE_VERSION=$($utils.Version)" >> $env:GITHUB_ENV
(Join-Path $saltsRoot "bin") >> $env:GITHUB_PATH
(Join-Path $saltsUtilsRoot "bin") >> $env:GITHUB_PATH
if ($Rid -eq "linux-x64") {
  $lib = @((Join-Path $saltsRoot "lib"), (Join-Path $saltsUtilsRoot "lib"))
  if ($env:LD_LIBRARY_PATH) { $lib += $env:LD_LIBRARY_PATH }
  "LD_LIBRARY_PATH=$($lib -join ':')" >> $env:GITHUB_ENV
}
Write-Host "curl restored latest stable Salts.Native -> $($salts.Version) for $Rid"
Write-Host "curl restored latest stable SaltsUtils.Native -> $($utils.Version) for $Rid"
Write-Host "Verified DataBind ABI 10 and salts-idlc: $idlc"
