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
                "MeshDescription" //For reading ID string of imported mesh, so a change can be detected.
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
		Link_SimulCasterServer(Target);
	}

	private string GetConfigName(ReadOnlyTargetRules Target)
	{
		string LibDirName;
		bool bDebug = (Target.Configuration == UnrealTargetConfiguration.Debug);
		if (bDebug)
		{
			LibDirName = "Debug";
		}
		else
		{
			LibDirName = "Release";
		}
		return LibDirName;

	}
    private void Link_libavstream(ReadOnlyTargetRules Target)
    {

        PrivateIncludePaths.Add(RemotePlayRootDirectory + "/libavstream/Include");
		string LibraryPath = Path.Combine(LibrariesDirectory, "lib/Release");
		PublicLibraryPaths.Add(LibraryPath);
		//PublicLibraryPaths.Add("C:/RemotePlay/plugins/UnrealDemo/Plugins/RemotePlay/Libraries/lib/Release");
		PublicAdditionalLibraries.Add("libavstream.lib");

		// SRT:
		string SrtLibraryPath = Path.Combine(LibrariesDirectory, "Release");
		PublicLibraryPaths.Add(SrtLibraryPath);
		PublicAdditionalLibraries.Add("srt_static.lib");
		string PthreadsLibraryPath = Path.Combine(RemotePlayRootDirectory, "thirdparty/srt/submodules/pthread-win32/bin/x64_MSVC2015.Release");
		PublicLibraryPaths.Add(PthreadsLibraryPath);
		PublicAdditionalLibraries.Add("pthread_lib.lib");
		PublicAdditionalLibraries.Add("ws2_32.lib");

        //set(PTHREAD_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/thirdparty/srt/submodules/pthread-win32)

        // EFP
        PublicAdditionalLibraries.Add("efp.lib");

        PublicDelayLoadDLLs.Add("libavstream.dll");
        RuntimeDependencies.Add(Path.Combine(LibraryPath, "libavstream.dll"));

        // Temporary path
        PublicLibraryPaths.Add("C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v10.1/lib/x64");

        PublicAdditionalLibraries.Add("cudart.lib");

		string Platform = GetPlatformName(Target);
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
        PublicLibraryPaths.Add(Path.Combine(LibrariesDirectory, "thirdparty/enet/Release"));

        PublicAdditionalLibraries.Add("enet.lib");
    }

    private void Link_basisu(ReadOnlyTargetRules Target)
    {

        PrivateIncludePaths.Add(Path.Combine(RemotePlayRootDirectory, "thirdparty/basis_universal"));

        PublicLibraryPaths.Add(Path.Combine(LibrariesDirectory, "thirdparty/basis_universal", GetConfigName(Target)));
		PublicLibraryPaths.Add(Path.Combine(LibrariesDirectory, "thirdparty/basis_universal/Release"));
		PublicLibraryPaths.Add(Path.Combine(LibrariesDirectory, "thirdparty/basis_universal/thirdparty/basis_universal", GetConfigName(Target)));
        PublicAdditionalLibraries.Add("basisu.lib");

        //PublicDelayLoadDLLs.Add("basisu_MD.dll");
        //RuntimeDependencies.Add(Path.Combine(LibraryPath, "basisu_MD.dll"));
    }

	public void Link_SimulCasterServer(ReadOnlyTargetRules Target)
    {
		PrivateIncludePaths.Add(Path.Combine(RemotePlayRootDirectory, "SimulCasterServer/src"));

		PublicLibraryPaths.Add(Path.Combine(LibrariesDirectory, "SimulCasterServer/Release"));
		PublicAdditionalLibraries.Add("SimulCasterServer.lib");
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

	//ModuleDirectory C:/Simul/RemotePlay/plugins/UnrealDemo/Plugins/RemotePlay/Source/RemotePlay
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

