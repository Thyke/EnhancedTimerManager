// Copyright (C) Thyke. All Rights Reserved.

#include "EnhancedTimerManagerSubsystem.h"
#include "Engine/World.h"
#include "Async/Async.h"

DEFINE_LOG_CATEGORY(LogEnhancedTimerManager);

void UEnhancedTimerManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Timers.Reserve(256);
    FiredThisTick.Reserve(128);
    ToRemove.Reserve(128);
    ToUnpause.Reserve(64);
    ReusableToFire.Reserve(128);
    ReusableSnapshot.Reserve(256);
}

void UEnhancedTimerManagerSubsystem::Deinitialize()
{
    Super::Deinitialize();
    {
        FWriteScopeLock _(MapLock);
        Timers.Empty();
    }
    FiredThisTick.Empty();
    ToRemove.Empty();
    ToUnpause.Empty();
    ReusableToFire.Empty();
    ReusableSnapshot.Empty();
    NextId = 1;
}

void UEnhancedTimerManagerSubsystem::EnforceGameThread() const
{
    if (!IsInGameThread())
    {
        UE_LOG(LogEnhancedTimerManager, Warning, TEXT("Public API called off the Game Thread. The call will be marshalled to GT."));
    }
}

bool UEnhancedTimerManagerSubsystem::IsGamePaused() const
{
    if (const UWorld* W = GetWorld())
    {
        return UGameplayStatics::IsGamePaused(W);
    }
    return false;
}

uint64 UEnhancedTimerManagerSubsystem::AllocateId()
{
    uint64 Out = NextId++;
    if (NextId == 0) { NextId = 1; } // wrap protection
    return Out;
}

bool UEnhancedTimerManagerSubsystem::GetData(uint64 Id, FEnhancedTimerData& Out) const
{
    FReadScopeLock _(MapLock);
    if (const FEnhancedTimerData* Found = Timers.Find(Id))
    {
        Out = *Found;
        return true;
    }
    return false;
}

FEnhancedTimerData* UEnhancedTimerManagerSubsystem::FindMutable(uint64 Id)
{
    FWriteScopeLock _(MapLock);
    return Timers.Find(Id);
}

FEnhancedTimerHandle UEnhancedTimerManagerSubsystem::SetEnhancedTimer(const FTimerDelegate& InDelegate,
                                                                      float Duration,
                                                                      EEnhancedTimerTimeDilationMode DilationMode,
                                                                      AActor* DilationActor,
                                                                      bool bAffectedByGamePause,
                                                                      bool bLoop,
                                                                      float DelayToStartCountingDown,
                                                                      float DelayToStartCountingDownVariation)
{
    EnforceGameThread();
    if (!IsInGameThread())
    {
        FTimerDelegate Copy = InDelegate;
        AsyncTask(ENamedThreads::GameThread, [this, Copy, Duration, DilationMode, DilationActor,
                                              bAffectedByGamePause, bLoop,
                                              DelayToStartCountingDown, DelayToStartCountingDownVariation]()
        {
            SetEnhancedTimer(Copy, Duration, DilationMode, DilationActor, bAffectedByGamePause,
                             bLoop, DelayToStartCountingDown, DelayToStartCountingDownVariation);
        });
        return FEnhancedTimerHandle(); // invalid handle when called off-thread; prefer SetEnhancedTimerAsync to capture the handle
    }

    FEnhancedTimerData Data;
    Data.Id                   = AllocateId();
    Data.Delegate             = InDelegate;
    Data.bUseDynamic          = false;
    Data.Duration             = FMath::Max(0.f, Duration);
    Data.PhaseElapsed         = 0.f;
    Data.InitialDelay         = 0.f;
    Data.Phase                = FEnhancedTimerData::ETimerPhase::Running;
    Data.bLoop                = bLoop;
    Data.bPaused              = false;
    Data.bAffectedByGamePause = bAffectedByGamePause;
    Data.DilationMode         = DilationMode;
    Data.DilationActor        = DilationActor;
    Data.bNextTick            = false;

    if (DelayToStartCountingDown > 0.f || DelayToStartCountingDownVariation > 0.f)
    {
        const float Rand   = FMath::FRandRange(-DelayToStartCountingDownVariation, DelayToStartCountingDownVariation);
        const float Bigger = FMath::Max(0.f, Rand);
        Data.InitialDelay   = FMath::Max(0.f, DelayToStartCountingDown + Bigger);
        if (Data.InitialDelay > 0.f)
        {
            Data.Phase       = FEnhancedTimerData::ETimerPhase::InitialDelay;
            Data.PhaseElapsed = 0.f;
        }
    }

    {
        FWriteScopeLock _(MapLock);
        Timers.Add(Data.Id, MoveTemp(Data));
    }

    return FEnhancedTimerHandle(Data.Id, this);
}

FEnhancedTimerHandle UEnhancedTimerManagerSubsystem::SetEnhancedTimerExecutedInNextTick(const FTimerDelegate& InDelegate)
{
    EnforceGameThread();
    if (!IsInGameThread())
    {
        FTimerDelegate Copy = InDelegate;
        AsyncTask(ENamedThreads::GameThread, [this, Copy]()
        {
            SetEnhancedTimerExecutedInNextTick(Copy);
        });
        return FEnhancedTimerHandle();
    }

    FEnhancedTimerData Data;
    Data.Id                   = AllocateId();
    Data.Delegate             = InDelegate;
    Data.bUseDynamic          = false;
    Data.Duration             = 0.f;
    Data.PhaseElapsed         = 0.f;
    Data.InitialDelay         = 0.f;
    Data.Phase                = FEnhancedTimerData::ETimerPhase::Running;
    Data.bLoop                = false;
    Data.bPaused              = false;
    Data.bAffectedByGamePause = true; // must fire even during game pause
    Data.DilationMode         = EEnhancedTimerTimeDilationMode::IgnoreTimeDilation;
    Data.bNextTick            = true;

    {
        FWriteScopeLock _(MapLock);
        Timers.Add(Data.Id, MoveTemp(Data));
    }

    return FEnhancedTimerHandle(Data.Id, this);
}

FEnhancedTimerHandle UEnhancedTimerManagerSubsystem::SetEnhancedTimer_BP(const UObject* /*WorldContextObject*/,
    const FTimerDynamicDelegate& Event,
    float Duration,
    EEnhancedTimerTimeDilationMode DilationMode,
    AActor* DilationActor,
    bool bAffectedByGamePause,
    bool bLoop,
    float DelayToStartCountingDown,
    float DelayToStartCountingDownVariation)
{
    EnforceGameThread();
    if (!IsInGameThread())
    {
        AsyncTask(ENamedThreads::GameThread, [this, Event, Duration, DilationMode, DilationActor,
                                              bAffectedByGamePause, bLoop,
                                              DelayToStartCountingDown, DelayToStartCountingDownVariation]()
        {
            SetEnhancedTimer_BP(nullptr, Event, Duration, DilationMode, DilationActor, bAffectedByGamePause,
                                bLoop, DelayToStartCountingDown, DelayToStartCountingDownVariation);
        });
        return FEnhancedTimerHandle();
    }

    FEnhancedTimerData Data;
    Data.Id                   = AllocateId();
    Data.DynamicDelegate      = Event;
    Data.bUseDynamic          = true;
    Data.Duration             = FMath::Max(0.f, Duration);
    Data.PhaseElapsed         = 0.f;
    Data.InitialDelay         = 0.f;
    Data.Phase                = FEnhancedTimerData::ETimerPhase::Running;
    Data.bLoop                = bLoop;
    Data.bPaused              = false;
    Data.bAffectedByGamePause = bAffectedByGamePause;
    Data.DilationMode         = DilationMode;
    Data.DilationActor        = DilationActor;
    Data.bNextTick            = false;

    if (DelayToStartCountingDown > 0.f || DelayToStartCountingDownVariation > 0.f)
    {
        const float Rand   = FMath::FRandRange(-DelayToStartCountingDownVariation, DelayToStartCountingDownVariation);
        const float Bigger = FMath::Max(0.f, Rand);
        Data.InitialDelay   = FMath::Max(0.f, DelayToStartCountingDown + Bigger);
        if (Data.InitialDelay > 0.f)
        {
            Data.Phase       = FEnhancedTimerData::ETimerPhase::InitialDelay;
            Data.PhaseElapsed = 0.f;
        }
    }

    {
        FWriteScopeLock _(MapLock);
        Timers.Add(Data.Id, MoveTemp(Data));
    }

    return FEnhancedTimerHandle(Data.Id, this);
}

FEnhancedTimerHandle UEnhancedTimerManagerSubsystem::SetEnhancedTimerExecutedInNextTick_BP(const UObject* /*WorldContextObject*/,
    const FTimerDynamicDelegate& Event)
{
    EnforceGameThread();
    if (!IsInGameThread())
    {
        AsyncTask(ENamedThreads::GameThread, [this, Event]()
        {
            SetEnhancedTimerExecutedInNextTick_BP(nullptr, Event);
        });
        return FEnhancedTimerHandle();
    }

    FEnhancedTimerData Data;
    Data.Id                   = AllocateId();
    Data.DynamicDelegate      = Event;
    Data.bUseDynamic          = true;
    Data.Duration             = 0.f;
    Data.PhaseElapsed         = 0.f;
    Data.InitialDelay         = 0.f;
    Data.Phase                = FEnhancedTimerData::ETimerPhase::Running;
    Data.bLoop                = false;
    Data.bPaused              = false;
    Data.bAffectedByGamePause = true;
    Data.DilationMode         = EEnhancedTimerTimeDilationMode::IgnoreTimeDilation;
    Data.bNextTick            = true;

    {
        FWriteScopeLock _(MapLock);
        Timers.Add(Data.Id, MoveTemp(Data));
    }

    return FEnhancedTimerHandle(Data.Id, this);
}

void UEnhancedTimerManagerSubsystem::Tick(float DeltaTime)
{
    UWorld* World = GetWorld();
    if (!World) return;

#if WITH_EDITOR || UE_BUILD_DEVELOPMENT
    const uint64 StartCycles = FPlatformTime::Cycles64();
    TimersProcessedLastTick = 0;
#endif

    const bool bPausedNow = IsGamePaused();

    // --- Snapshot phase (short read lock) ---
    ReusableSnapshot.Reset(Timers.Num());
    {
        FReadScopeLock RLock(MapLock);
        for (const auto& Pair : Timers)
        {
            ReusableSnapshot.Emplace(Pair.Key, Pair.Value);
        }
    }

    // --- Quick pass without lock: pre-mark next-tick timers ---
    for (const auto& Pair : ReusableSnapshot)
    {
        const FEnhancedTimerData& T = Pair.Value;

        if (T.bPaused) continue;
        if (bPausedNow && !T.bAffectedByGamePause) continue;

        if (T.bNextTick)
        {
            FiredThisTick.Add(Pair.Key);
            continue;
        }
    }

    // --- Mutable pass: update elapsed / phases and collect fires (single write lock) ---
    {
        FWriteScopeLock WLock(MapLock);
        for (TPair<uint64, FEnhancedTimerData>& Pair : Timers)
        {
            FEnhancedTimerData& T = Pair.Value;

            if (T.bPaused) continue;
            if (bPausedNow && !T.bAffectedByGamePause) continue;

            if (T.bNextTick) continue;

            const float Eff = T.GetEffectiveDelta(DeltaTime, World);
            T.Advance(Eff);

            if (T.TryTransitFromDelay())
            {
                // Just transitioned to Running; do not fire on transition.
                continue;
            }

            if (T.ShouldFire())
            {
                FiredThisTick.Add(Pair.Key);
            }

#if WITH_EDITOR || UE_BUILD_DEVELOPMENT
            ++TimersProcessedLastTick;
#endif
        }
    }

    ExecuteFired();
    Cleanup();

#if WITH_EDITOR || UE_BUILD_DEVELOPMENT
    const uint64 EndCycles = FPlatformTime::Cycles64();
    LastTickTimeMs = FPlatformTime::ToMilliseconds64(EndCycles - StartCycles);
#endif
}

void UEnhancedTimerManagerSubsystem::ExecuteFired()
{
    if (FiredThisTick.Num() == 0) return;

    // Use reusable buffer to avoid per-tick allocations.
    ReusableToFire.Reset(FiredThisTick.Num());
    ReusableToFire.Append(FiredThisTick);
    FiredThisTick.Reset();

    for (uint64 Id : ReusableToFire)
    {
        FEnhancedTimerData Copy;
        const bool bHave = GetData(Id, Copy);
        if (!bHave) continue;

        // Execute the bound delegate
        if (Copy.bUseDynamic)
        {
            if (Copy.DynamicDelegate.IsBound())
            {
                Copy.DynamicDelegate.ProcessDelegate<UObject>(nullptr);
            }
        }
        else
        {
            if (Copy.Delegate.IsBound())
            {
                Copy.Delegate.Execute();
            }
        }

        // Post-fire handling
        if (FEnhancedTimerData* Mut = FindMutable(Id))
        {
            if (Mut->bLoop)
            {
                // For looping timers, reset phase to Running and elapsed to 0.
                Mut->Phase        = FEnhancedTimerData::ETimerPhase::Running;
                Mut->PhaseElapsed = 0.f;
                Mut->bNextTick    = false;
            }
            else
            {
                ToRemove.Add(Id);
            }
        }
    }
}

void UEnhancedTimerManagerSubsystem::Cleanup()
{
    if (ToRemove.Num() == 0 && ToUnpause.Num() == 0) return;

    FWriteScopeLock _(MapLock);

    for (uint64 Id : ToRemove)
    {
        Timers.Remove(Id);
    }
    ToRemove.Reset();

    for (uint64 Id : ToUnpause)
    {
        if (FEnhancedTimerData* T = Timers.Find(Id))
        {
            T->bPaused = false;
        }
    }
    ToUnpause.Reset();
}

// ===== Single-handle operations =====

bool UEnhancedTimerManagerSubsystem::IsTimerValid(const FEnhancedTimerHandle& Handle) const
{
    if (Handle.Id == 0) return false;
    FReadScopeLock _(MapLock);
    return Timers.Contains(Handle.Id);
}

void UEnhancedTimerManagerSubsystem::InvalidateTimer(const FEnhancedTimerHandle& Handle)
{
    EnforceGameThread();
    if (!IsInGameThread())
    {
        AsyncTask(ENamedThreads::GameThread, [this, Handle]() { InvalidateTimer(Handle); });
        return;
    }

    if (Handle.Id == 0) return;
    FWriteScopeLock _(MapLock);
    Timers.Remove(Handle.Id);
}

bool UEnhancedTimerManagerSubsystem::IsTimerPaused(const FEnhancedTimerHandle& Handle) const
{
    FEnhancedTimerData T;
    return GetData(Handle.Id, T) ? T.bPaused : false;
}

void UEnhancedTimerManagerSubsystem::PauseTimer(const FEnhancedTimerHandle& Handle)
{
    EnforceGameThread();
    if (!IsInGameThread())
    {
        AsyncTask(ENamedThreads::GameThread, [this, Handle]() { PauseTimer(Handle); });
        return;
    }

    if (FEnhancedTimerData* T = FindMutable(Handle.Id))
    {
        T->bPaused = true;
    }
}

void UEnhancedTimerManagerSubsystem::UnpauseTimer(const FEnhancedTimerHandle& Handle)
{
    EnforceGameThread();
    if (!IsInGameThread())
    {
        AsyncTask(ENamedThreads::GameThread, [this, Handle]() { UnpauseTimer(Handle); });
        return;
    }

    if (FEnhancedTimerData* T = FindMutable(Handle.Id))
    {
        T->bPaused = false;
    }
}

bool UEnhancedTimerManagerSubsystem::IsTimerLooping(const FEnhancedTimerHandle& Handle) const
{
    FEnhancedTimerData T;
    return GetData(Handle.Id, T) ? T.bLoop : false;
}

float UEnhancedTimerManagerSubsystem::GetTimerDuration(const FEnhancedTimerHandle& Handle) const
{
    FEnhancedTimerData T;
    return GetData(Handle.Id, T) ? T.Duration : -1.f;
}

float UEnhancedTimerManagerSubsystem::GetTimerTimeLeft(const FEnhancedTimerHandle& Handle) const
{
    FEnhancedTimerData T;
    if (!GetData(Handle.Id, T)) return -1.f;

    if (T.Phase == FEnhancedTimerData::ETimerPhase::InitialDelay)
    {
        const float RemDelay = FMath::Max(0.f, T.InitialDelay - T.PhaseElapsed);
        return RemDelay;
    }
    return FMath::Max(0.f, T.Duration - T.PhaseElapsed);
}

float UEnhancedTimerManagerSubsystem::GetTimerElapsedTime(const FEnhancedTimerHandle& Handle) const
{
    FEnhancedTimerData T;
    return GetData(Handle.Id, T) ? T.PhaseElapsed : -1.f;
}

bool UEnhancedTimerManagerSubsystem::IsTimerAffectedByGamePause(const FEnhancedTimerHandle& Handle) const
{
    FEnhancedTimerData T;
    return GetData(Handle.Id, T) ? T.bAffectedByGamePause : false;
}

EEnhancedTimerTimeDilationMode UEnhancedTimerManagerSubsystem::GetTimerTimeDilationMode(const FEnhancedTimerHandle& Handle) const
{
    FEnhancedTimerData T;
    return GetData(Handle.Id, T) ? T.DilationMode : EEnhancedTimerTimeDilationMode::IgnoreTimeDilation;
}

// ===== Bulk operations =====

void UEnhancedTimerManagerSubsystem::InvalidateAllTimers()
{
    EnforceGameThread();
    if (!IsInGameThread())
    {
        AsyncTask(ENamedThreads::GameThread, [this]() { InvalidateAllTimers(); });
        return;
    }

    FWriteScopeLock _(MapLock);
    Timers.Empty();
}

void UEnhancedTimerManagerSubsystem::PauseAllTimers()
{
    EnforceGameThread();
    if (!IsInGameThread())
    {
        AsyncTask(ENamedThreads::GameThread, [this]() { PauseAllTimers(); });
        return;
    }

    FWriteScopeLock _(MapLock);
    for (TPair<uint64, FEnhancedTimerData>& Pair : Timers)
    {
        Pair.Value.bPaused = true;
    }
}

void UEnhancedTimerManagerSubsystem::UnpauseAllTimers()
{
    EnforceGameThread();
    if (!IsInGameThread())
    {
        AsyncTask(ENamedThreads::GameThread, [this]() { UnpauseAllTimers(); });
        return;
    }

    FWriteScopeLock _(MapLock);
    for (TPair<uint64, FEnhancedTimerData>& Pair : Timers)
    {
        Pair.Value.bPaused = false;
    }
}

#if WITH_EDITOR || UE_BUILD_DEVELOPMENT
void UEnhancedTimerManagerSubsystem::DumpActiveTimers() const
{
    FReadScopeLock RLock(MapLock);
    UE_LOG(LogEnhancedTimerManager, Log, TEXT("Active timers: %d, LastTick=%.3f ms, Processed=%d"),
        Timers.Num(), LastTickTimeMs, TimersProcessedLastTick);

    for (const auto& P : Timers)
    {
        const auto& T = P.Value;
        UE_LOG(LogEnhancedTimerManager, Log, TEXT("  [%llu] Phase=%d Elapsed=%.3f Dur=%.3f Delay=%.3f Loop=%d Paused=%d NextTick=%d Mode=%d"),
            P.Key,
            (int32)T.Phase,
            T.PhaseElapsed,
            T.Duration,
            T.InitialDelay,
            (int32)T.bLoop,
            (int32)T.bPaused,
            (int32)T.bNextTick,
            (int32)T.DilationMode);
    }
}
#endif
