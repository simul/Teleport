// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RemotePlayEditorModule.h"
#include "RemotePlay.h"
#include "RemotePlaySettings.h"
#include "RemotePlayEditorModule.h"
#include "Modules/ModuleManager.h"
#include "IAssetTypeActions.h"
#include "AssetToolsModule.h"
#include "Misc/ConfigCacheIni.h"
#include "ISettingsModule.h"
#include "AssetRegistryModule.h"
#include "ThumbnailRendering/ThumbnailManager.h"

IMPLEMENT_MODULE(FRemotePlayEditorModule, RemotePlayEditor);

#define LOCTEXT_NAMESPACE "RemotePlayEditorModule"

EAssetTypeCategories::Type FRemotePlayEditorModule::RemotePlayAssetCategory;


FRemotePlayEditorModule::FRemotePlayEditorModule()
	
{
}


void FRemotePlayEditorModule::StartupModule()
{
	MenuExtensibilityManager = MakeShareable(new FExtensibilityManager);
	ToolBarExtensibilityManager = MakeShareable(new FExtensibilityManager);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	RemotePlayAssetCategory = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("RemotePlay")), LOCTEXT("RemotePlayAssetsCategory", "RemotePlay"));

	URemotePlaySettings::OnSettingsChanged().AddRaw(this, &FRemotePlayEditorModule::OnRemotePlaySettingsChangedEvent);

}


void FRemotePlayEditorModule::ShutdownModule()
{
}

FRemotePlayEditorModule& FRemotePlayEditorModule::Get()
{
	return FModuleManager::LoadModuleChecked<FRemotePlayEditorModule>("RemotePlayEditor");
}

void FRemotePlayEditorModule::OnRemotePlaySettingsChangedEvent(const FString& PropertyName, const URemotePlaySettings* Settings)
{
}

#undef LOCTEXT_NAMESPACE
