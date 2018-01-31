// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;
using System.Collections.Generic;

public class RemotePlayClientTarget : TargetRules
{
	public RemotePlayClientTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Client;

		ExtraModuleNames.AddRange( new string[] { "RemotePlay" } );
	}
}
