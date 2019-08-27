// Copyright 2018 Simul.co

using UnrealBuildTool;
using System.IO;

public class RemotePlay : ModuleRules
{
	public RemotePlay(ReadOnlyTargetRules Target) : base(Target)
	{
		OptimizeCode = CodeOptimization.Never;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PrivateIncludePaths.AddRange(
			new string[] {
				"RemotePlay/Private",
				"RemotePlayEditor/Private",
			}
			);
			
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
                "InputCore",
                "Sockets",
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
                "Projects",
                "Networking",
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
        Link_basisu(Target);
	}

    private void Link_libavstream(ReadOnlyTargetRules Target)
    {
		string LibraryPath = Path.Combine(LibrariesDirectory,"libavstream/lib/Release");
        string Platform = GetPlatformName(Target);

        PrivateIncludePaths.Add(RemotePlayRootDirectory + "/libavstream/Include");
        PublicLibraryPaths.Add(LibraryPath);
		PublicAdditionalLibraries.Add("libavstream.lib");

        PublicDelayLoadDLLs.Add("libavstream.dll");
        RuntimeDependencies.Add(Path.Combine(LibraryPath, "libavstream.dll"));

        // Temporary path
        PublicLibraryPaths.Add("C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v10.1/lib/x64");

        PublicAdditionalLibraries.Add("cudart.lib");

        if (Platform == "Win64" || Platform == "Win32")
        {
            PublicAdditionalLibraries.Add("dxgi.lib");
            PublicAdditionalLibraries.Add("d3d12.lib");
            string SystemPath = "C:/Windows/System32";
            RuntimeDependencies.Add(Path.Combine(SystemPath, "dxgi.dll"));
            RuntimeDependencies.Add(Path.Combine(SystemPath, "D3D12.dll"));
        }
    }

    private void Link_libenet(ReadOnlyTargetRules Target)
    {
	    PrivateIncludePaths.Add(Path.Combine(RemotePlayRootDirectory, "thirdparty/enet/Include"));
        
        // Only remove a path below if you're sure its not needed for anyone
        PublicLibraryPaths.Add(Path.Combine(RemotePlayRootDirectory, "build/x64/thirdparty/enet/Release"));
        PublicLibraryPaths.Add(Path.Combine(RemotePlayRootDirectory, "thirdparty/enet/Release"));
        PublicLibraryPaths.Add(Path.Combine(RemotePlayRootDirectory, "thirdparty/enet/x64/Release"));
        PublicLibraryPaths.Add(Path.Combine(LibrariesDirectory, "enet/Release"));

        PublicAdditionalLibraries.Add("enet.lib");
    }

    private void Link_basisu(ReadOnlyTargetRules Target)
    {
        string LibraryPath = Path.Combine(RemotePlayRootDirectory, "thirdparty/basis_universal/bin");

        PrivateIncludePaths.Add(Path.Combine(RemotePlayRootDirectory, "thirdparty/basis_universal"));

        PublicLibraryPaths.Add(LibraryPath);
        PublicAdditionalLibraries.Add("basisu_MD.lib");

        PublicDelayLoadDLLs.Add("basisu_MD.dll");
        RuntimeDependencies.Add(Path.Combine(LibraryPath, "basisu_MD.dll"));
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

	//ModuleDirectory C:\Simul\RemotePlay\plugins\UnrealDemo\Plugins\RemotePlay\Source\RemotePlay
    private string LibrariesDirectory
    {
        get
        {
			return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../Libraries/"));
        }
	}

	private string RemotePlayRootDirectory
	{
		get
		{
			return Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../../../../"));
		}
	}
}

