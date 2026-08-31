// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AI_MMORPG_Client : ModuleRules
{
	public AI_MMORPG_Client(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"AI_MMORPG_Client",
			"AI_MMORPG_Client/Variant_Platforming",
			"AI_MMORPG_Client/Variant_Platforming/Animation",
			"AI_MMORPG_Client/Variant_Combat",
			"AI_MMORPG_Client/Variant_Combat/AI",
			"AI_MMORPG_Client/Variant_Combat/Animation",
			"AI_MMORPG_Client/Variant_Combat/Gameplay",
			"AI_MMORPG_Client/Variant_Combat/Interfaces",
			"AI_MMORPG_Client/Variant_Combat/UI",
			"AI_MMORPG_Client/Variant_SideScrolling",
			"AI_MMORPG_Client/Variant_SideScrolling/AI",
			"AI_MMORPG_Client/Variant_SideScrolling/Gameplay",
			"AI_MMORPG_Client/Variant_SideScrolling/Interfaces",
			"AI_MMORPG_Client/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
