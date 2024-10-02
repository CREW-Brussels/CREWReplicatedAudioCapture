// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Audio.h"
#include "AudioAnalytics.h"
#include "AudioCapture.h"
#include "AudioCaptureDeviceInterface.h"
#include "Components/SynthComponent.h"
#include "ReplicatedAudioCaptureComponent.generated.h"

UCLASS(ClassGroup = Synth, meta = (BlueprintSpawnableComponent))
class REPLICATEDAUDIOCAPTURE_API UReplicatedAudioCaptureComponent : public USynthComponent
{
	GENERATED_BODY()

protected:

	UReplicatedAudioCaptureComponent(const FObjectInitializer& ObjectInitializer);

	bool StartCapture();

	void InitNetwork();
	void AddAudioData(TArray<float> &data);

	//~ Begin USynthComponent interface
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;
	virtual void OnBeginGenerate() override;
	virtual void OnEndGenerate() override;
	//~ End USynthComponent interface

	//~ Begin UObject interface
	virtual void BeginPlay() override;
	virtual void BeginDestroy() override;
	virtual bool IsReadyForFinishDestroy() override;
	virtual void FinishDestroy() override;
	//~ End UObject interface
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Device")
	FString DeviceInputName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio Device")
	FName StreamName;

	void OnData(const void* AudioData, int32 NumFrames, int32 NumChannels, int32 SampleRate);
private:
	friend class UReplicatedAudioCaptureSubsystem;

	TArray<float> CaptureAudioData;
	int32 CapturedAudioDataSamples;

	bool bSuccessfullyInitialized;
	bool bIsCapturing;
	bool bIsStreamOpen;

	int32 CaptureChannels;
	int32 FramesSinceStarting;
	int32 ReadSampleIndex;
	FThreadSafeBool bIsDestroying;

	int32 NetworkSamplRate;

	int32 deviceIndex;


	int32 NumSamplesEnqueued;
	Audio::FCaptureDeviceInfo CaptureInfo;
	Audio::FAudioCapture AudioCapture;
	FCriticalSection CaptureCriticalSection;
	TArray<float> AudioCaptureData;
	bool bInitialized;
	bool bStreamingOverride;

	Audio::FOnAudioCaptureFunction captureFunction;
};