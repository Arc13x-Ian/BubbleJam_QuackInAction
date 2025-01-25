/*******************************************************************************
The content of this file includes portions of the proprietary AUDIOKINETIC Wwise
Technology released in source code form as part of the game integration package.
The content of this file may not be used without valid licenses to the
AUDIOKINETIC Wwise Technology.
Note that the use of the game engine is subject to the Unreal(R) Engine End User
License Agreement at https://www.unrealengine.com/en-US/eula/unreal
 
License Usage
 
Licensees holding valid licenses to the AUDIOKINETIC Wwise Technology may use
this file in accordance with the end user license agreement provided with the
software or, alternatively, in accordance with the terms contained
in a written agreement between you and Audiokinetic Inc.
Copyright (c) 2024 Audiokinetic Inc.
*******************************************************************************/

using UnrealBuildTool;

public class WwiseAuthoring : ModuleRules
{
	public WwiseAuthoring(ReadOnlyTargetRules Target) : base(Target)
	{
		WwiseSoundEngine_2022_1.ApplyWaapi(this, Target);
		WwiseSoundEngine_2023_1.ApplyWaapi(this, Target);
		WwiseSoundEngine_2024_1.ApplyWaapi(this, Target, true);		// Latest version should be written with "latest" to true for logging purposes

		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"WwiseSoundEngine",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
		});

#if UE_5_3_OR_LATER
		bLegacyParentIncludePaths = false;
		CppStandard = CppStandardVersion.Default;
#endif
	}
}
