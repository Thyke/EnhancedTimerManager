// Copyright (C) Thyke. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EnhancedTimerManagerTypes.h" // ensure enum is visible
#include "EnhancedTimerHandle.generated.h"

class UEnhancedTimerManagerSubsystem;

/**
 * Lightweight handle that identifies a timer stored in the subsystem.
 * It forwards convenience calls to the owning subsystem.
 */
USTRUCT(BlueprintType)
struct ENHANCEDTIMERMANAGER_API FEnhancedTimerHandle
{
	GENERATED_BODY()

public:
	/** 0 == Invalid */
	UPROPERTY(VisibleAnywhere, Category="EnhancedTimers")
	uint64 Id = 0;

	/** Non-UProperty weak owner reference (not serialized). */
	TWeakObjectPtr<UEnhancedTimerManagerSubsystem> Owner;

	FEnhancedTimerHandle() = default;
	explicit FEnhancedTimerHandle(uint64 InId, UEnhancedTimerManagerSubsystem* InOwner)
		: Id(InId), Owner(InOwner) {}

	/** C++ convenience API (Blueprint versions exist on the subsystem). */
	bool        IsValid() const;
	void        Invalidate();
	bool        IsPaused() const;
	void        Pause();
	void        Unpause();
	bool        IsLooping() const;
	float       GetDuration() const;
	float       GetTimeLeft() const;
	float       GetElapsedTime() const;
	bool        IsAffectedByGamePause() const;
	EEnhancedTimerTimeDilationMode GetTimeDilationMode() const;

	bool operator==(const FEnhancedTimerHandle& Other) const { return Id == Other.Id && Owner == Other.Owner; }
	bool operator!=(const FEnhancedTimerHandle& Other) const { return !(*this == Other); }

	FORCEINLINE bool HasOwner() const { return Owner.IsValid(); }
};
