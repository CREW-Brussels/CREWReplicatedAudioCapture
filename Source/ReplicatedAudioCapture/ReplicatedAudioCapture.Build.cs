// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class ReplicatedAudioCapture : ModuleRules
	{
		public ReplicatedAudioCapture(ReadOnlyTargetRules Target) : base(Target)
		{
            PublicDependencyModuleNames.AddRange(
                new string[] {
                    "Core",
                    "CoreUObject",
                    "Engine",
                    "AudioMixer",
                    "AudioCaptureCore",
                    "AudioCapture",
                    "Sockets",
                    "Networking"
                }
            );

            if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
            {
                PrivateDependencyModuleNames.Add("AudioCaptureWasapi");
                PrivateDependencyModuleNames.Add("AudioCaptureRtAudio");
            }
            else if (Target.Platform == UnrealTargetPlatform.Mac)
            {
                PrivateDependencyModuleNames.Add("AudioCaptureRtAudio");
            }
            else if (Target.Platform == UnrealTargetPlatform.IOS)
            {
                PrivateDependencyModuleNames.Add("AudioCaptureAudioUnit");
            }
            else if (Target.Platform == UnrealTargetPlatform.Android)
            {
                PrivateDependencyModuleNames.Add("AudioCaptureAndroid");
            }
        }
    }
}
