// Copyright (C) Thyke. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EnhancedTimerManagerTypes.generated.h"

/** Timer time-dilation behavior. */
UENUM(BlueprintType)
enum class EEnhancedTimerTimeDilationMode : uint8
{
	/** Timer ignores both global and actor time dilation (i.e., real-world delta). */
	IgnoreTimeDilation UMETA(DisplayName="Ignore"),

	/** Timer scales with global time dilation. */
	GlobalTimeDilation UMETA(DisplayName="Global"),

	/** Timer scales with a specific Actor's CustomTimeDilation (fallback to Ignore if Actor is invalid). */
	ActorTimeDilation  UMETA(DisplayName="Actor")
};
