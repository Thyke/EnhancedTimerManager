// Copyright (C) Thyke. All Rights Reserved.

#include "EnhancedTimerHandle.h"
#include "EnhancedTimerManagerSubsystem.h"

bool FEnhancedTimerHandle::IsValid() const
{
	return Id != 0 && Owner.IsValid() && Owner->IsTimerValid(*this);
}

void FEnhancedTimerHandle::Invalidate()
{
	if (Owner.IsValid()) Owner->InvalidateTimer(*this);
}

bool FEnhancedTimerHandle::IsPaused() const
{
	return Owner.IsValid() ? Owner->IsTimerPaused(*this) : false;
}

void FEnhancedTimerHandle::Pause()
{
	if (Owner.IsValid()) Owner->PauseTimer(*this);
}

void FEnhancedTimerHandle::Unpause()
{
	if (Owner.IsValid()) Owner->UnpauseTimer(*this);
}

bool FEnhancedTimerHandle::IsLooping() const
{
	return Owner.IsValid() ? Owner->IsTimerLooping(*this) : false;
}

float FEnhancedTimerHandle::GetDuration() const
{
	return Owner.IsValid() ? Owner->GetTimerDuration(*this) : -1.f;
}

float FEnhancedTimerHandle::GetTimeLeft() const
{
	return Owner.IsValid() ? Owner->GetTimerTimeLeft(*this) : -1.f;
}

float FEnhancedTimerHandle::GetElapsedTime() const
{
	return Owner.IsValid() ? Owner->GetTimerElapsedTime(*this) : -1.f;
}

bool FEnhancedTimerHandle::IsAffectedByGamePause() const
{
	return Owner.IsValid() ? Owner->IsTimerAffectedByGamePause(*this) : false;
}

EEnhancedTimerTimeDilationMode FEnhancedTimerHandle::GetTimeDilationMode() const
{
	return Owner.IsValid() ? Owner->GetTimerTimeDilationMode(*this) : EEnhancedTimerTimeDilationMode::IgnoreTimeDilation;
}
