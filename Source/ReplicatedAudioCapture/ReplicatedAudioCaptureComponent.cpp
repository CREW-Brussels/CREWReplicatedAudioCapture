// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReplicatedAudioCaptureComponent.h"
#include "ReplicatedAudioCaptureSubsystem.h"


static const unsigned int MaxBufferSize = 2 * 5 * 48000;

UReplicatedAudioCaptureComponent::UReplicatedAudioCaptureComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSuccessfullyInitialized = false;
	bIsCapturing = false;
	CapturedAudioDataSamples = 0;
	ReadSampleIndex = 0;
	bIsDestroying = false;
	bIsStreamOpen = false;
	CaptureAudioData.Reserve(2 * 48000 * 5);
	deviceIndex = -1;
	NetworkSamplRate = 48000;
}

bool UReplicatedAudioCaptureComponent::StartCapture()
{
	Audio::FCaptureDeviceInfo DeviceInfo;
	TArray<Audio::FCaptureDeviceInfo> devices;
	int deviceCount = AudioCapture.GetCaptureDevicesAvailable(devices);
	deviceIndex = -1;
	for (int i = 0; i < devices.Num(); i++) {
		UE_LOG(LogAudio, Warning, TEXT("%s"), *devices[i].DeviceName);
		if (devices[i].DeviceName.StartsWith(DeviceInputName)) {
			DeviceInfo = devices[i];
			deviceIndex = i;
		}
	}
	if (deviceIndex == -1) {
		return false;
	}

	if (DeviceInfo.PreferredSampleRate > 0)
	{
		//SampleRate = DeviceInfo.PreferredSampleRate;
	}
	else
	{
		UE_LOG(LogAudio, Warning, TEXT("Attempted to open a capture device with the invalid SampleRate value of %i"), DeviceInfo.PreferredSampleRate);
	}
	NumChannels = DeviceInfo.InputChannels;

	if (NumChannels > 0 && NumChannels <= 8)
	{
		bool bSuccess = true;
		if (!bIsCapturing)
		{
			captureFunction = [this](const void* AudioData, int32 NumFrames, int32 InNumChannels, int32 InSampleRate, double StreamTime, bool bOverFlow)
				{
					int32 NumSamples = InNumChannels * NumFrames;

					if (bIsCapturing)
					{
						if (GetWorld() && GetWorld()->GetGameInstance()) {
							UReplicatedAudioCaptureSubsystem* system = GetWorld()->GetGameInstance()->GetSubsystem<UReplicatedAudioCaptureSubsystem>();
							if (system) {
								FScopeLock Lock(&CaptureCriticalSection);
								CaptureAudioData.Empty();
								int32 InNumSamples = InNumChannels * NumFrames;
								int32 Index = CaptureAudioData.AddUninitialized(InNumSamples);
								float* AudioCaptureDataPtr = CaptureAudioData.GetData();

								//Avoid reading outside of buffer boundaries
								if (!(InNumSamples > MaxBufferSize))
								{
									FMemory::Memcpy(&AudioCaptureDataPtr[Index], AudioData, InNumSamples * sizeof(float));
									system->SendNetworkAudio(StreamName, InSampleRate, InNumChannels, CaptureAudioData);
								}
							}
						}
					}
				};

			// Prepare the audio buffer memory for 2 seconds of stereo audio at 48k SR to reduce chance for allocation in callbacks
			AudioCaptureData.Reserve(2 * 2 * 48000);

			Audio::FAudioCaptureDeviceParams Params = Audio::FAudioCaptureDeviceParams();
			Params.DeviceIndex = deviceIndex;

			// Start the stream here to avoid hitching the audio render thread. 
			if (AudioCapture.OpenAudioCaptureStream(Params, captureFunction, 1024))
			{
				AudioCapture.StartStream();
			}
			else
			{
				return false;
			}
		}

		{
			FScopeLock Lock(&CaptureCriticalSection);
			AudioCaptureData.Reset();
			bIsCapturing = true;
		}

		Audio::Analytics::RecordEvent_Usage(TEXT("AudioCapture.AudioCaptureComponentInitialized"));

		return true;
	}
	else
	{
		UE_LOG(LogAudio, Warning, TEXT("Attempted to open a device with the invalid NumChannels value of %i"), NumChannels);
	}
	return false;
}

void UReplicatedAudioCaptureComponent::InitNetwork() {

}

void UReplicatedAudioCaptureComponent::AddAudioData(TArray<float>& data) {
	FScopeLock Lock(&CaptureCriticalSection);
	if (!(AudioCaptureData.Num() + data.Num() > MaxBufferSize)) {
		AudioCaptureData.Append(data);
	}
	else {
		UE_LOG(LogAudio, Warning, TEXT("Attempt to write past end of buffer in AddAudioData [%u]"), AudioCaptureData.Num() + data.Num());
	}
}

bool UReplicatedAudioCaptureComponent::Init(int32& SampleRate)
{
	SampleRate = NetworkSamplRate;
	return true;
}

void UReplicatedAudioCaptureComponent::BeginPlay()
{
	if (GetWorld() && GetWorld()->GetGameInstance()) {
		UReplicatedAudioCaptureSubsystem* system = GetWorld()->GetGameInstance()->GetSubsystem<UReplicatedAudioCaptureSubsystem>();
		if (system) {
			system->RegisterReplicatedAudioCapture(this);
		}
	}
}

void UReplicatedAudioCaptureComponent::BeginDestroy()
{
	
	if (GetWorld() && GetWorld()->GetGameInstance()) {
		UReplicatedAudioCaptureSubsystem* system = GetWorld()->GetGameInstance()->GetSubsystem<UReplicatedAudioCaptureSubsystem>();
		if (system) {
			system->UnRegisterReplicatedAudioCapture(this);
		}
	}
	Super::BeginDestroy();

	// Flag that we're beginning to be destroyed
	// This is so that if a mic capture is open, we shut it down on the render thread
	bIsDestroying = true;

	if (bIsCapturing)
	{
		AudioCapture.AbortStream();
		AudioCapture.CloseStream();
		bIsCapturing = false;
	}

	// Make sure stop is kicked off
	if (bIsStreamOpen) {
		Stop();
	}
}

bool UReplicatedAudioCaptureComponent::IsReadyForFinishDestroy()
{
	//Prevent an infinite loop here if it was marked pending kill while generating
	OnEndGenerate();

	return true;
}

void UReplicatedAudioCaptureComponent::FinishDestroy()
{
	if (bIsCapturing)
	{
		AudioCapture.AbortStream();
		AudioCapture.CloseStream();
		bIsCapturing = false;
	}
	Super::FinishDestroy();
	bSuccessfullyInitialized = false;
	bIsDestroying = false;
}

void UReplicatedAudioCaptureComponent::OnBeginGenerate()
{
	CapturedAudioDataSamples = 0;
	ReadSampleIndex = 0;
	CaptureAudioData.Reset();
}

void UReplicatedAudioCaptureComponent::OnEndGenerate()
{

}

int32 UReplicatedAudioCaptureComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	int32 OutputSamplesGenerated = 0;
	//In case of severe overflow, just drop the data
	if (CaptureAudioData.Num() > MaxBufferSize)
	{
		FScopeLock Lock(&CaptureCriticalSection);

		int32 CaptureDataSamples = AudioCaptureData.Num();
		if (CaptureDataSamples > 0)
		{
			// Append the capture audio to the output buffer
			int32 OutIndex = CaptureAudioData.AddUninitialized(CaptureDataSamples);
			float* OutDataPtr = CaptureAudioData.GetData();

			//Check bounds of buffer
			if (!(OutIndex > MaxBufferSize))
			{
				FMemory::Memcpy(&OutDataPtr[OutIndex], AudioCaptureData.GetData(), CaptureDataSamples * sizeof(float));
				AudioCaptureData.Reset();
			}
			else
			{
				UE_LOG(LogAudio, Warning, TEXT("Attempt to write past end of buffer in GetAudioData"));
			}
		}
		CaptureAudioData.Reset();
		return NumSamples;
	}
	int InNumSamplesEnqueued = 0;
	{
		FScopeLock Lock(&CaptureCriticalSection);
		InNumSamplesEnqueued = AudioCaptureData.Num();
	}

	if (CapturedAudioDataSamples > 0 || InNumSamplesEnqueued > 1024)
	{
		// Check if we need to read more audio data from capture synth
		if (ReadSampleIndex + NumSamples > CaptureAudioData.Num())
		{
			// but before we do, copy off the remainder of the capture audio data buffer if there's data in it
			int32 SamplesLeft = FMath::Max(0, CaptureAudioData.Num() - ReadSampleIndex);
			if (SamplesLeft > 0)
			{
				float* CaptureDataPtr = CaptureAudioData.GetData();
				if (!(ReadSampleIndex + NumSamples > MaxBufferSize - 1))
				{
					FMemory::Memcpy(OutAudio, &CaptureDataPtr[ReadSampleIndex], SamplesLeft * sizeof(float));
				}
				else
				{
					UE_LOG(LogAudio, Warning, TEXT("Attempt to write past end of buffer in OnGenerateAudio, when we got more data from the synth"));
					return NumSamples;
				}
				// Track samples generated
				OutputSamplesGenerated += SamplesLeft;
			}
			// Get another block of audio from the capture synth
			CaptureAudioData.Reset();
			{
				FScopeLock Lock(&CaptureCriticalSection);

				int32 CaptureDataSamples = AudioCaptureData.Num();
				if (CaptureDataSamples > 0)
				{
					// Append the capture audio to the output buffer
					int32 OutIndex = CaptureAudioData.AddUninitialized(CaptureDataSamples);
					float* OutDataPtr = CaptureAudioData.GetData();

					//Check bounds of buffer
					if (!(OutIndex > MaxBufferSize))
					{
						FMemory::Memcpy(&OutDataPtr[OutIndex], AudioCaptureData.GetData(), CaptureDataSamples * sizeof(float));
						AudioCaptureData.Reset();
					}
					else
					{
						UE_LOG(LogAudio, Warning, TEXT("Attempt to write past end of buffer in GetAudioData"));
					}
				}
				//CaptureAudioData.Reset();
			}
			// Reset the read sample index since we got a new buffer of audio data
			ReadSampleIndex = 0;
		}
		// note it's possible we didn't get any more audio in our last attempt to get it
		if (CaptureAudioData.Num() > 0)
		{
			// Compute samples to copy
			int32 NumSamplesToCopy = FMath::Min(NumSamples - OutputSamplesGenerated, CaptureAudioData.Num() - ReadSampleIndex);
			float* CaptureDataPtr = CaptureAudioData.GetData();
			if (!(ReadSampleIndex + NumSamplesToCopy > MaxBufferSize - 1))
			{
				FMemory::Memcpy(&OutAudio[OutputSamplesGenerated], &CaptureDataPtr[ReadSampleIndex], NumSamplesToCopy * sizeof(float));
			}
			else
			{
				UE_LOG(LogAudio, Warning, TEXT("Attempt to read past end of buffer in OnGenerateAudio, when we did not get more data from the synth"));
				return NumSamples;
			}
			ReadSampleIndex += NumSamplesToCopy;
			OutputSamplesGenerated += NumSamplesToCopy;
		}
		CapturedAudioDataSamples += OutputSamplesGenerated;
	}
	else
	{
		// Say we generated the full samples, this will result in silence
		OutputSamplesGenerated = NumSamples;
	}
	return OutputSamplesGenerated;
}

void UReplicatedAudioCaptureComponent::OnData(const void* AudioData, int32 NumFrames, int32 In_NumChannels, int32 SampleRate) {
	if (GetWorld() && GetWorld()->GetGameInstance()) {
		UReplicatedAudioCaptureSubsystem* system = GetWorld()->GetGameInstance()->GetSubsystem<UReplicatedAudioCaptureSubsystem>();
		if (system) {
			CaptureAudioData.Empty();
			int32 NumSamples = In_NumChannels * NumFrames;
			int32 Index = CaptureAudioData.AddUninitialized(NumSamples);
			float* AudioCaptureDataPtr = CaptureAudioData.GetData();

			//Avoid reading outside of buffer boundaries
			if (!(NumSamples > MaxBufferSize))
			{
				FMemory::Memcpy(&AudioCaptureDataPtr[Index], AudioData, NumSamples * sizeof(float));
				system->SendNetworkAudio(StreamName, SampleRate, In_NumChannels, CaptureAudioData);
			}

			//system->SendNetworkAudio(StreamName, SampleRate, In_NumChannels, CaptureAudioData);
		}
	}
}
