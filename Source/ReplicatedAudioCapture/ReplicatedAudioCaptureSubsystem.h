// Copyright HTC Corporation. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "IPAddress.h"
#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketReceiver.h"
#include "Common/UdpSocketSender.h"
#include "ReplicatedAudioCaptureSubsystem.generated.h"

class UReplicatedAudioCaptureComponent;

USTRUCT()
struct FReplicatedAudioCaptureStreams
{
	GENERATED_BODY()
	UPROPERTY()
	TArray<UReplicatedAudioCaptureComponent *> streams;
};

//DECLARE_LOG_CATEGORY_EXTERN(LogViveCustomHandGesture, Log, All);
UCLASS()
class REPLICATEDAUDIOCAPTURE_API UReplicatedAudioCaptureSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	UReplicatedAudioCaptureSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void RegisterReplicatedAudioCapture(UReplicatedAudioCaptureComponent* component);
	void UnRegisterReplicatedAudioCapture(UReplicatedAudioCaptureComponent* component);

	void SendNetworkAudio(FName name, int32 sampleRate, int32 channelNum, TArray<float>& data);

private:
	UPROPERTY()
	TMap<FName, FReplicatedAudioCaptureStreams> components;

	FSocket* Socket;
	FUdpSocketReceiver* UDPReceiver;
	bool IsServer;

	FCriticalSection NetworkCriticalSection;
	TSharedPtr<FInternetAddr> MulticastAddr;
	TArray<uint8> networkBuffer;
};
