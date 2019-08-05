// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RemotePlayEditor : ModuleRules
{
	public RemotePlayEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoSharedPCHs;
		PrivateIncludePaths.AddRange(new string[] {
			"RemotePlayEditor/Private",
			"RemotePlay/Private"
		});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Engine",
				"RHI",
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"InputCore",
				"RenderCore",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"UnrealEd",
				"RemotePlay",
				"TimeManagement",
				"PropertyEditor",
				"TargetPlatform",
				"AppFramework",
				"Projects",
				"MainFrame",
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Engine",
				"Messaging",
				"LevelEditor",
				"AssetTools",
				"ContentBrowser",
				"DerivedDataCache",
				"RemotePlay"
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"RemotePlay",
				"Engine",
				"UnrealEd",
			}
		);

		PublicIncludePathModuleNames.AddRange(
			new string[] {
				"Engine",
				"RemotePlay"
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"WorkspaceMenuStructure",
				}
			);
	}
}
