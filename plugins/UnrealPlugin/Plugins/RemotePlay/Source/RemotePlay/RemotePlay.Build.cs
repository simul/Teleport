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
				"Slate",
				"SlateCore",
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
	}

    private void Link_libavstream(ReadOnlyTargetRules Target)
    {
        string LibraryPath = null;
        switch (Target.Platform)
        {
            case UnrealTargetPlatform.Win64:
                LibraryPath = Path.Combine(LibrariesDirectory, "libavstream/Win64");
                break;
            case UnrealTargetPlatform.Win32:
                LibraryPath = Path.Combine(LibrariesDirectory, "libavstream/Win32");
                break;
        }

        PrivateIncludePaths.Add(Path.Combine(LibrariesDirectory, "libavstream/Include"));
        PublicLibraryPaths.Add(LibraryPath);

        PublicAdditionalLibraries.Add("libavstream.lib");
        PublicDelayLoadDLLs.Add("libavstream.dll");
        RuntimeDependencies.Add(Path.Combine(LibraryPath, "libavstream.dll"));
    }

    private string LibrariesDirectory
    {
        get
        {
            return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../Libraries/"));
        }
    }
}
