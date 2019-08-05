// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "AssetTypeCategories.h"
#include "IAssetTools.h"
class URemotePlaySettings;

/** RemotePlay Editor module */
class FRemotePlayEditorModule : public IModuleInterface
{
public:
public:
	FRemotePlayEditorModule();

	// IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Get the instance of this module. */
	REMOTEPLAYEDITOR_API static FRemotePlayEditorModule& Get();

	/** Register/unregister niagara editor settings. */
	void RegisterSettings();
	void UnregisterSettings();

	static EAssetTypeCategories::Type GetAssetCategory() { return RemotePlayAssetCategory; }


	/** Get the  UI commands. */
	REMOTEPLAYEDITOR_API const class FRemotePlayditorCommands& Commands();

private:
	void RegisterAssetTypeAction(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action);
	void OnRemotePlaySettingsChangedEvent(const FString& PropertyName, const URemotePlaySettings* Settings);
	void OnPreGarbageCollection();

private:
	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager;
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager;

	/** All created asset type actions.  Cached here so that we can unregister it during shutdown. */
	TArray< TSharedPtr<IAssetTypeActions> > CreatedAssetTypeActions;


	static EAssetTypeCategories::Type RemotePlayAssetCategory;

};


