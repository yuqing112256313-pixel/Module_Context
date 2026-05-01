param(
    [string]$Repo = '',
    [string]$OutputRoot = '',
    [string]$CMake = 'C:\toolchains\portable\cmake-4.3.2-windows-x86_64\bin\cmake.exe',
    [string]$SevenZip = 'C:\toolchains\portable\7zip-26.00\Files\7-Zip\7z.exe',
    [string]$Generator = 'Visual Studio 14 2015 Win64',
    [string]$PackageName = 'Foundation-1.1.0-vs2015-x64',
    [switch]$SkipZip
)

$ErrorActionPreference = 'Stop'

function Resolve-RepoRoot {
    if (-not [string]::IsNullOrWhiteSpace($Repo)) {
        if (-not (Test-Path -LiteralPath $Repo -PathType Container)) {
            throw "Repo root not found: $Repo"
        }
        return (Resolve-Path -LiteralPath $Repo).Path
    }

    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

function Resolve-Executable {
    param(
        [string]$PreferredPath,
        [string]$FallbackCommand,
        [string]$Label
    )

    if (-not [string]::IsNullOrWhiteSpace($PreferredPath) -and
        (Test-Path -LiteralPath $PreferredPath -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $PreferredPath).Path
    }

    $command = Get-Command $FallbackCommand -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    throw "$Label not found. Provide -$Label or put $FallbackCommand on PATH."
}

function Assert-VisualStudio2015X64Generator {
    param([string]$GeneratorName)

    if ($GeneratorName -ne 'Visual Studio 14 2015 Win64') {
        throw "This package must be built with 'Visual Studio 14 2015 Win64' for VS2015 x64 ABI compatibility. Got: $GeneratorName"
    }
}

function Assert-SafeOutputRoot {
    param([string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $root = [System.IO.Path]::GetPathRoot($fullPath)
    if ($fullPath.TrimEnd('\') -eq $root.TrimEnd('\')) {
        throw "Refusing to use a drive root as OutputRoot: $fullPath"
    }
}

function New-CleanDirectory {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
}

function Ensure-File {
    param(
        [string]$Path,
        [string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label not found: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Copy-RequiredFile {
    param(
        [string]$Source,
        [string]$Destination
    )

    Copy-Item -LiteralPath (Ensure-File -Path $Source -Label 'Required file') -Destination $Destination -Force
}

function Assert-ProjectUsesV140 {
    param([string]$BuildDir)

    $projectPath = Join-Path $BuildDir 'foundation.vcxproj'
    Ensure-File -Path $projectPath -Label 'Generated Visual Studio project' | Out-Null

    $matches = Select-String `
        -Path $projectPath `
        -Pattern '<PlatformToolset>v140</PlatformToolset>' `
        -SimpleMatch

    if ($null -eq $matches) {
        throw "Generated project does not use PlatformToolset v140: $projectPath"
    }
}

function Assert-PackageArtifacts {
    param([string]$PackageRoot)

    $required = @(
        'include\foundation\base\Export.h',
        'Debug\Lib\foundationd.lib',
        'Debug\Dll\foundationd.dll',
        'Release\Lib\foundation.lib',
        'Release\Dll\foundation.dll',
        'Foundation-vs2015-x64.props',
        'README-VS2015.txt'
    )

    foreach ($relativePath in $required) {
        $path = Join-Path $PackageRoot $relativePath
        Ensure-File -Path $path -Label 'Package artifact' | Out-Null
    }
}

function Write-ConsumerProps {
    param([string]$Path)

    $content = @'
<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="14.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup Label="UserMacros">
    <FoundationRoot>$(MSBuildThisFileDirectory)</FoundationRoot>
  </PropertyGroup>

  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalIncludeDirectories>$(FoundationRoot)include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>FOUNDATION_SHARED_LIBRARY;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>

  <ItemDefinitionGroup Condition="'$(Configuration)'=='Debug'">
    <ClCompile>
      <RuntimeLibrary>MultiThreadedDebugDLL</RuntimeLibrary>
    </ClCompile>
    <Link>
      <AdditionalLibraryDirectories>$(FoundationRoot)Debug\Lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <AdditionalDependencies>foundationd.lib;%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
    <PostBuildEvent>
      <Command>xcopy /Y /D "$(FoundationRoot)Debug\Dll\foundationd.dll" "$(OutDir)"</Command>
    </PostBuildEvent>
  </ItemDefinitionGroup>

  <ItemDefinitionGroup Condition="'$(Configuration)'=='Release'">
    <ClCompile>
      <RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>
    </ClCompile>
    <Link>
      <AdditionalLibraryDirectories>$(FoundationRoot)Release\Lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <AdditionalDependencies>foundation.lib;%(AdditionalDependencies)</AdditionalDependencies>
    </Link>
    <PostBuildEvent>
      <Command>xcopy /Y /D "$(FoundationRoot)Release\Dll\foundation.dll" "$(OutDir)"</Command>
    </PostBuildEvent>
  </ItemDefinitionGroup>
</Project>
'@

    Set-Content -LiteralPath $Path -Value $content -Encoding UTF8
}

function Write-PackageReadme {
    param(
        [string]$Path,
        [string]$PackageName
    )

    $content = @"
$PackageName

This package contains Foundation built as a VS2015 x64 shared library.

Layout:
  include\foundation\...
  Debug\Lib\foundationd.lib
  Debug\Dll\foundationd.dll
  Release\Lib\foundation.lib
  Release\Dll\foundation.dll
  Foundation-vs2015-x64.props

Visual Studio 2015 usage:
  1. Import Foundation-vs2015-x64.props into the consuming .vcxproj, or configure the same values manually.
  2. Define FOUNDATION_SHARED_LIBRARY for all consumers.
  3. Use the DLL CRT: /MDd for Debug and /MD for Release.
  4. Debug links Debug\Lib\foundationd.lib and needs Debug\Dll\foundationd.dll at runtime.
  5. Release links Release\Lib\foundation.lib and needs Release\Dll\foundation.dll at runtime.

Do not replace these files with MinGW-built binaries for VS2015 projects.
Foundation exposes C++ and STL types, so the import library and DLL must use the MSVC v140 ABI.
"@

    Set-Content -LiteralPath $Path -Value $content -Encoding UTF8
}

function Write-Manifest {
    param(
        [string]$Root,
        [string]$Path
    )

    $entries = Get-ChildItem -LiteralPath $Root -Recurse -File |
        Where-Object { $_.FullName -ne $Path } |
        Sort-Object FullName |
        ForEach-Object {
            $relative = $_.FullName.Substring($Root.Length).TrimStart('\')
            $hash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
            "{0}  {1}" -f $hash.Hash.ToLowerInvariant(), $relative.Replace('\', '/')
        }

    Set-Content -LiteralPath $Path -Value $entries -Encoding UTF8
}

function Compress-Package {
    param(
        [string]$SourceRoot,
        [string]$ZipPath,
        [string]$SevenZipPath
    )

    if (Test-Path -LiteralPath $ZipPath) {
        Remove-Item -LiteralPath $ZipPath -Force
    }

    if (-not [string]::IsNullOrWhiteSpace($SevenZipPath) -and
        (Test-Path -LiteralPath $SevenZipPath -PathType Leaf)) {
        Push-Location -LiteralPath $SourceRoot
        try {
            & $SevenZipPath a -tzip $ZipPath '.\*'
        } finally {
            Pop-Location
        }
    } else {
        Compress-Archive -Path (Join-Path $SourceRoot '*') -DestinationPath $ZipPath -Force
    }
}

$RepoRoot = Resolve-RepoRoot
$SourceRoot = Join-Path $RepoRoot 'third_party\Foundation'
$DefaultOutputRoot = Join-Path $RepoRoot 'out\foundation-vs2015-package'
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = $DefaultOutputRoot
}
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$BuildDir = Join-Path $OutputRoot '_build-vs2015-x64'
$PackageRoot = Join-Path $OutputRoot $PackageName
$ZipPath = "$PackageRoot.zip"

Assert-VisualStudio2015X64Generator -GeneratorName $Generator
Assert-SafeOutputRoot -Path $OutputRoot

if (-not (Test-Path -LiteralPath (Join-Path $SourceRoot 'CMakeLists.txt') -PathType Leaf)) {
    throw "Foundation source directory not found: $SourceRoot"
}

$CMakeExe = Resolve-Executable -PreferredPath $CMake -FallbackCommand 'cmake' -Label 'CMake'

Write-Host "[foundation] repo: $RepoRoot"
Write-Host "[foundation] source: $SourceRoot"
Write-Host "[foundation] output: $OutputRoot"
Write-Host "[foundation] cmake: $CMakeExe"
Write-Host "[foundation] generator: $Generator"

New-CleanDirectory -Path $BuildDir
New-CleanDirectory -Path $PackageRoot
if (Test-Path -LiteralPath $ZipPath) {
    Remove-Item -LiteralPath $ZipPath -Force
}

& $CMakeExe -S $SourceRoot -B $BuildDir -G $Generator `
    -DFOUNDATION_BUILD_SHARED=ON `
    -DFOUNDATION_BUILD_TESTS=OFF `
    -DFOUNDATION_BUILD_EXAMPLES=OFF `
    -DCMAKE_DEBUG_POSTFIX=d

Assert-ProjectUsesV140 -BuildDir $BuildDir

& $CMakeExe --build $BuildDir --config Release --target foundation -- /m
& $CMakeExe --build $BuildDir --config Debug --target foundation -- /m

New-Item -ItemType Directory -Force `
    (Join-Path $PackageRoot 'include'),
    (Join-Path $PackageRoot 'Debug\Lib'),
    (Join-Path $PackageRoot 'Debug\Dll'),
    (Join-Path $PackageRoot 'Release\Lib'),
    (Join-Path $PackageRoot 'Release\Dll') | Out-Null

Copy-Item -LiteralPath (Join-Path $SourceRoot 'include\foundation') `
    -Destination (Join-Path $PackageRoot 'include') `
    -Recurse `
    -Force

Copy-RequiredFile -Source (Join-Path $BuildDir 'Release\foundation.lib') -Destination (Join-Path $PackageRoot 'Release\Lib\foundation.lib')
Copy-RequiredFile -Source (Join-Path $BuildDir 'Release\foundation.dll') -Destination (Join-Path $PackageRoot 'Release\Dll\foundation.dll')
Copy-RequiredFile -Source (Join-Path $BuildDir 'Debug\foundationd.lib') -Destination (Join-Path $PackageRoot 'Debug\Lib\foundationd.lib')
Copy-RequiredFile -Source (Join-Path $BuildDir 'Debug\foundationd.dll') -Destination (Join-Path $PackageRoot 'Debug\Dll\foundationd.dll')

Write-ConsumerProps -Path (Join-Path $PackageRoot 'Foundation-vs2015-x64.props')
Write-PackageReadme -Path (Join-Path $PackageRoot 'README-VS2015.txt') -PackageName $PackageName
Write-Manifest -Root $PackageRoot -Path (Join-Path $PackageRoot 'SHA256SUMS.txt')
Assert-PackageArtifacts -PackageRoot $PackageRoot

if (-not $SkipZip) {
    Compress-Package -SourceRoot $PackageRoot -ZipPath $ZipPath -SevenZipPath $SevenZip
}

Write-Host "[foundation] package directory: $PackageRoot"
if (-not $SkipZip) {
    Write-Host "[foundation] package zip: $ZipPath"
}
