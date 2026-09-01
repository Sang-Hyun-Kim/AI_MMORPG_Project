$ueDir = "..\..\Client\AMC1\Source\AMC1\Network"
$filesToPatch = @("Enum.pb.cc", "Enum.pb.h", "Protocol.pb.cc", "Protocol.pb.h", "Struct.pb.cc", "Struct.pb.h")

foreach ($file in $filesToPatch) {
    $filePath = Join-Path $ueDir $file
    if (Test-Path $filePath) {
        $content = Get-Content $filePath -Raw
        
        # 이미 패치되었는지 확인
        if ($content -notmatch "THIRD_PARTY_INCLUDES_START") {
            $prefix = @"
#if UE_BUILD_DEBUG + UE_BUILD_DEVELOPMENT + UE_BUILD_TEST + UE_BUILD_SHIPPING >= 1
#include "CoreMinimal.h"
#pragma push_macro("check")
#pragma push_macro("verify")
#pragma push_macro("ensure")
#pragma push_macro("cast")
#undef check
#undef verify
#undef ensure
#undef cast
THIRD_PARTY_INCLUDES_START
#pragma warning(push)
#pragma warning(disable: 4127 4668 4800 4583 4582 5054 4244 4267 4946 4125 4647)
#pragma pack(push, 8)
#endif

"@
            
            $suffix = @"

#if UE_BUILD_DEBUG + UE_BUILD_DEVELOPMENT + UE_BUILD_TEST + UE_BUILD_SHIPPING >= 1
#pragma pack(pop)
#pragma warning(pop)
THIRD_PARTY_INCLUDES_END
#pragma pop_macro("cast")
#pragma pop_macro("ensure")
#pragma pop_macro("verify")
#pragma pop_macro("check")
#endif
"@
            
            $newContent = $prefix + $content + $suffix
            Set-Content -Path $filePath -Value $newContent
            Write-Host "Patched $file for UE macros"
        }
    }
}
Write-Host "Done!"
