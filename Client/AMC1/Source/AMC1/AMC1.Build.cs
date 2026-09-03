// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AMC1 : ModuleRules
{
	public AMC1(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		// [2026-09-04] "HTTP", "Json", "JsonUtilities" 추가 — 결함 F5
		//   C# 인증 백엔드(/api/auth/login)에 로그인 요청을 보내고 응답 JSON에서
		//   티켓을 파싱하기 위해 필요합니다. 이 모듈이 없으면 AMC1GameInstance의
		//   FHttpModule / FJsonSerializer 사용부가 링크 에러로 실패합니다.
		//   ⚠️ 이 줄을 수정하면 증분 빌드가 아니라 전체 재빌드가 발생합니다.
		//      Binaries/ 와 Intermediate/ 정리 후 프로젝트 파일 재생성이 필요할 수 있습니다.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Sockets", "Networking", "ProtobufCore", "HTTP", "Json", "JsonUtilities" });

		PrivateDependencyModuleNames.AddRange(new string[] {  });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
