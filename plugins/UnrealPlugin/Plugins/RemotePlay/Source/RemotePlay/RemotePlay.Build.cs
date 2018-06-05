// Copyright 2018 Simul.co

using UnrealBuildTool;
using System.IO;

public class RemotePlay : ModuleRules
{
	public RemotePlay(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				"RemotePlay/Public"
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				"RemotePlay/Private",
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
                "RHI",
				"Slate",
				"SlateCore",
                "RenderCore",
                "ShaderCore",
                "Projects"
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

        Link_libavstream(Target);
        Link_libenet(Target);
	}

    private void Link_libavstream(ReadOnlyTargetRules Target)
    {
        string LibraryPath = Path.Combine(LibrariesDirectory, "libavstream", GetPlatformName(Target));

        PrivateIncludePaths.Add(Path.Combine(LibrariesDirectory, "libavstream/Include"));
        PublicLibraryPaths.Add(LibraryPath);

        PublicAdditionalLibraries.Add("libavstream.lib");
        PublicDelayLoadDLLs.Add("libavstream.dll");
        RuntimeDependencies.Add(Path.Combine(LibraryPath, "libavstream.dll"));
    }

    private void Link_libenet(ReadOnlyTargetRules Target)
    {
        PrivateIncludePaths.Add(Path.Combine(LibrariesDirectory, "enet/Include"));
        PublicLibraryPaths.Add(Path.Combine(LibrariesDirectory, "enet", GetPlatformName(Target)));
        PublicAdditionalLibraries.Add("enet.lib");
    }

    private string GetPlatformName(ReadOnlyTargetRules Target)
    {
        switch(Target.Platform)
        {
            case UnrealTargetPlatform.Win64:
                return "Win64";
            case UnrealTargetPlatform.Win32:
                return "Win32";
            default:
                return "Unknown";
        }
    }

    private string LibrariesDirectory
    {
        get
        {
            return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../Libraries/"));
        }
    }
}
