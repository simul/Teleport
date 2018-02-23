// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class RemotePlayPlugin : ModuleRules
{
	public RemotePlayPlugin(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				"RemotePlayPlugin/Public"
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				"RemotePlayPlugin/Private",
                Path.Combine(ThirdPartyDirectory, "libstreaming/Include"),
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
                "RHI",
                "RenderCore",
                "ShaderCore",
                "D3D11RHI"
			}
			);

        switch (Target.Platform)
        {
            case UnrealTargetPlatform.Win64:
                PublicAdditionalLibraries.Add(Path.Combine(ThirdPartyDirectory, "libstreaming/Win64/libstreaming.lib"));
                PublicAdditionalLibraries.Add(Path.Combine(ThirdPartyDirectory, "libstreaming/Win64/enet.lib"));
                break;
            case UnrealTargetPlatform.Win32:
                // TODO: Implement.
                break;
        }
   	}

    private string ThirdPartyDirectory
    {
        get
        {
            return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/"));
        }
    }
}
