param([Parameter(Mandatory=$true)][string]$LinuxZip,
      [string]$Output = (Join-Path $PWD '!Scale-v0.7.0-Windows-x64.zip'))

$ErrorActionPreference = 'Stop'
$LinuxZip = (Resolve-Path -LiteralPath $LinuxZip).Path
$Output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue) -or $env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    throw 'Run this script in an x64 Native Tools Command Prompt for Visual Studio with the Desktop development with C++ workload installed.'
}
$work = Join-Path ([IO.Path]::GetTempPath()) ('scale-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $work | Out-Null
Push-Location $work
try {
    $sdl = '2.32.10'
    Invoke-WebRequest -UseBasicParsing "https://github.com/libsdl-org/SDL/releases/download/release-$sdl/SDL2-devel-$sdl-VC.zip" -OutFile sdl.zip
    if ((Get-FileHash -LiteralPath sdl.zip -Algorithm SHA256).Hash -ne "af347939395a58b365846aaea27391e69f9ec9d4dd650d6ac40802159b418a6e") {
        throw "The SDL2 SDK checksum did not match."
    }
    Expand-Archive -LiteralPath sdl.zip -DestinationPath .
    $sdk = Join-Path $work "SDL2-$sdl"
    $lib = Join-Path $sdk 'lib/x64'
    $package = Join-Path $work 'package'
    Expand-Archive -LiteralPath $LinuxZip -DestinationPath $package
    Remove-Item -LiteralPath (Join-Path $package 'scale-frontier'), (Join-Path $package 'Play-!Scale.sh')
    $sources = @('main', 'contact-rig', 'world-picker', 'world', 'shovel', 'render', 'game', 'micro', 'expedition',
                 'game-expedition', 'terrain-tests', 'atmosphere', 'settlement', 'game-settlement') |
        ForEach-Object { Join-Path $PSScriptRoot "src/$_.cpp" }
    $exe = Join-Path $package '!Scale.exe'
    & cl.exe /nologo /std:c++20 /permissive- /EHsc /O2 /MT /utf-8 @sources "/Fe:$exe" /link /MACHINE:X64 (Join-Path $lib 'SDL2.lib') user32.lib
    if ($LASTEXITCODE) { throw 'The Windows build failed.' }
    Copy-Item -LiteralPath (Join-Path $lib 'SDL2.dll') -Destination $package
    Copy-Item -LiteralPath (Join-Path $sdk 'LICENSE.txt') -Destination (Join-Path $package 'SDL2-LICENSE.txt')
    @'
Extract the entire ZIP, then double-click !Scale.exe.
This build requires 64-bit Windows and a graphics driver that supports OpenGL 3.3.
Saves and launch-error.log are stored in %LOCALAPPDATA%\scale-frontier.
For windowed mode, run .\!Scale.exe --windowed from PowerShell.
'@ | Set-Content -LiteralPath (Join-Path $package 'README.txt')
    Remove-Item -LiteralPath (Join-Path $package 'README.md')
    & $exe --check-install
    if ($LASTEXITCODE) { throw 'The Windows bundle failed its installation check.' }
    & $exe --self-test
    if ($LASTEXITCODE) { throw 'The Windows game model tests failed.' }
    Compress-Archive -Path (Join-Path $package '*') -DestinationPath $Output -Force
} finally {
    Pop-Location
    Remove-Item -Recurse -Force -LiteralPath $work
}
