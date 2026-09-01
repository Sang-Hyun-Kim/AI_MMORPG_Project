$vcpkgDir = "d:\Mydev\AI_MMORPG_Project\vcpkg\installed\x64-windows-unreal"
$destDir = "d:\Mydev\AI_MMORPG_Project\Client\AMC1\Source\ProtobufCore"

New-Item -Path "$destDir\Include" -ItemType Directory -Force
New-Item -Path "$destDir\Lib\Win64" -ItemType Directory -Force

Copy-Item -Path "$vcpkgDir\include\google" -Destination "$destDir\Include" -Recurse -Force
Copy-Item -Path "$vcpkgDir\include\absl" -Destination "$destDir\Include" -Recurse -Force

Copy-Item -Path "$vcpkgDir\lib\*.lib" -Destination "$destDir\Lib\Win64" -Force
Write-Host "Protobuf and Abseil headers and libraries copied to ProtobufCore successfully!"
