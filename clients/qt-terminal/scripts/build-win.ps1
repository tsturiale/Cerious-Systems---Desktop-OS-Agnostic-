param(
  [string]$QtPrefix = $env:Qt6_DIR
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root 'build/windows-msvc'

$Args = @('-S', $Root, '-B', $Build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release')
if ($QtPrefix) {
  $Args += "-DCMAKE_PREFIX_PATH=$QtPrefix"
}

cmake @Args
cmake --build $Build --parallel
