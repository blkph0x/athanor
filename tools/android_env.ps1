# Builder env for REQ-4.x (DEC-0015). Not a product dependency.
$env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
$env:JAVA_HOME = 'C:\Program Files\Java\jdk-19'
$ndk = Join-Path $env:ANDROID_HOME 'ndk\27.3.13750724'
$env:ANDROID_NDK = $ndk
$pre = Join-Path $ndk 'toolchains\llvm\prebuilt\windows-x86_64\bin'
Write-Output "ANDROID_HOME=$env:ANDROID_HOME"
Write-Output "ANDROID_NDK=$env:ANDROID_NDK"
Write-Output "JAVA_HOME=$env:JAVA_HOME"
if (Test-Path (Join-Path $pre 'clang.exe')) {
    Write-Output "NDK clang OK"
} else {
    Write-Output "NDK clang MISSING"
}
if (Test-Path (Join-Path (Get-Location) 'vendor\knox\knoxsdk.jar')) {
    Write-Output "knoxsdk.jar present"
} else {
    Write-Output "knoxsdk.jar ABSENT — stub build only (Partner Program download)"
}
