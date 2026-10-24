param([switch]$Asan,[string]$SDKDirectory=(Join-Path $PSScriptRoot 'SDK'))
$ErrorActionPreference='Stop'
$build=Join-Path $PSScriptRoot $(if($Asan){'obj\AsanCurrent'}else{'obj\Build'})
$arguments=@('-S',$PSScriptRoot,'-B',$build,'-A','x64',('-DSDK_DIRECTORY='+$SDKDirectory),('-DENABLE_ASAN='+$(if($Asan){'ON'}else{'OFF'})))
if($Asan){
    $vs=& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if(!$vs){throw 'Visual Studio C++ tools are required.'}
    $toolset=Get-ChildItem -LiteralPath (Join-Path $vs 'VC\Tools\MSVC') -Directory | Where-Object {[version]$_.Name -ge [version]'14.38.0' -and (Test-Path -LiteralPath (Join-Path $_.FullName 'bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll'))} | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
    if(!$toolset){throw 'Install the current Visual Studio C++ AddressSanitizer component.'}
    $arguments+=@('-T',('version='+$toolset.Name));$savedPath=$env:PATH
    $env:PATH=(Join-Path $toolset.FullName 'bin\Hostx64\x64')+';'+$env:PATH
}
try{
    & cmake @arguments
    if($LASTEXITCODE){throw 'CMake configuration failed.'}
    $buildArguments=@();if($Asan){$buildArguments=@('--',('/p:VCToolsVersion='+$toolset.Name),'/p:PreferredToolArchitecture=x64')}
    & cmake --build $build --config Release --parallel 2 @buildArguments
    if($LASTEXITCODE){throw 'C++ build failed.'}
    & ctest --test-dir $build -C Release --output-on-failure
    if($LASTEXITCODE){throw 'Offline tests failed.'}
    Write-Host ('Offline build and tests passed: '+(Join-Path $build 'bin\InputCIRunner.exe'))
}finally{if($Asan){$env:PATH=$savedPath}}
