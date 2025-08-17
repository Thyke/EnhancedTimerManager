// Copyright (C) Thyke. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "TimerManager.h"                // FTimerDelegate / FTimerDynamicDelegate
#include "Kismet/GameplayStatics.h"
#include "EnhancedTimerManagerTypes.h"
#include "EnhancedTimerHandle.h"
#include "Engine/World.h" 
#include "Stats/Stats.h"
#include "EnhancedTimerManagerSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogEnhancedTimerManager, Log, All);

/** Per-timer internal state (not exposed as USTRUCT). */
struct FEnhancedTimerData
{
    /** Phase machine to make initial delay deterministic and simple. */
    enum class ETimerPhase : uint8
    {
        InitialDelay,
        Running
    };

    uint64                                 Id = 0;
    FTimerDelegate                         Delegate;             // C++ delegate (void return)
    FTimerDynamicDelegate                  DynamicDelegate;      // Blueprint delegate
    bool                                   bUseDynamic = false;  // true if DynamicDelegate is bound
    bool                                   bLoop = false;
    bool                                   bPaused = false;
    bool                                   bAffectedByGamePause = false;
    bool                                   bNextTick = false;

    float                                  Duration = 0.f;       // seconds for Running phase
    float                                  PhaseElapsed = 0.f;   // elapsed in current phase
    float                                  InitialDelay = 0.f;   // seconds for InitialDelay phase
    ETimerPhase                            Phase = ETimerPhase::Running;

    EEnhancedTimerTimeDilationMode         DilationMode = EEnhancedTimerTimeDilationMode::IgnoreTimeDilation;
    TWeakObjectPtr<AActor>                 DilationActor;

    /** Compute effective delta time considering dilation mode. */
    FORCEINLINE float GetEffectiveDelta(float WorldDelta, const UWorld* World) const
    {
        switch (DilationMode)
        {
            case EEnhancedTimerTimeDilationMode::GlobalTimeDilation:
                return World ? (WorldDelta * UGameplayStatics::GetGlobalTimeDilation(World)) : WorldDelta;
            case EEnhancedTimerTimeDilationMode::ActorTimeDilation:
            {
                const AActor* A = DilationActor.Get();
                if (A)
                {
                    const float Scale = FMath::Max(UE_SMALL_NUMBER, A->CustomTimeDilation);
                    return WorldDelta * Scale;
                }
                // Fallback: actor invalid -> behave like IgnoreTimeDilation
                return WorldDelta;
            }
            default:
                return WorldDelta;
        }
    }

    /** Advance the phase timer. */
    FORCEINLINE void Advance(float EffDelta) { PhaseElapsed += EffDelta; }

    /** Transition from InitialDelay to Running; returns true if transition happened. */
    FORCEINLINE bool TryTransitFromDelay()
    {
        if (Phase == ETimerPhase::InitialDelay && PhaseElapsed + KINDA_SMALL_NUMBER >= InitialDelay)
        {
            Phase = ETimerPhase::Running;
            PhaseElapsed = 0.f;
            return true;
        }
        return false;
    }

    /** Should fire in current phase? (only Running uses Duration threshold) */
    FORCEINLINE bool ShouldFire() const
    {
        return (Phase == ETimerPhase::Running) && (PhaseElapsed + KINDA_SMALL_NUMBER >= Duration);
    }
};

/**
 * GameInstanceSubsystem + FTickableGameObject that manages time-dilation-aware timers.
 * All public API is intended to be used on the Game Thread; if called from other threads,
 * the call is marshalled back to the Game Thread.
 */
UCLASS(BlueprintType)
class ENHANCEDTIMERMANAGER_API UEnhancedTimerManagerSubsystem
    : public UGameInstanceSubsystem
    , public FTickableGameObject
{
    GENERATED_BODY()

public:
    // ===== UGameInstanceSubsystem =====
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ===== FTickableGameObject =====
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UEnhancedTimerManagerSubsystem, STATGROUP_Tickables); }
    virtual bool IsTickable() const override { return true; }
    virtual bool IsTickableWhenPaused() const override { return true; } // per-timer pause gate via bAffectedByGamePause

    // ========================= C++ API =========================

    /** Create a timer (one-shot or looping) with a C++ delegate. */
    FEnhancedTimerHandle SetEnhancedTimer(const FTimerDelegate& InDelegate,
                                          float Duration,
                                          EEnhancedTimerTimeDilationMode DilationMode = EEnhancedTimerTimeDilationMode::IgnoreTimeDilation,
                                          AActor* DilationActor = nullptr,
                                          bool bAffectedByGamePause = false,
                                          bool bLoop = false,
                                          float DelayToStartCountingDown = 0.f,
                                          float DelayToStartCountingDownVariation = 0.f);

    /** Execute a delegate on the next tick (frame). */
    FEnhancedTimerHandle SetEnhancedTimerExecutedInNextTick(const FTimerDelegate& InDelegate);

    /**
     * Optional helper: Invoke SetEnhancedTimer on the Game Thread and return the handle via a completion callback.
     * Does not break the existing API; useful when called off the Game Thread.
     */
    template<typename OnCompleteType>
    void SetEnhancedTimerAsync(const FTimerDelegate& InDelegate,
                               float Duration,
                               EEnhancedTimerTimeDilationMode DilationMode,
                               AActor* DilationActor,
                               bool bAffectedByGamePause,
                               bool bLoop,
                               float DelayToStartCountingDown,
                               float DelayToStartCountingDownVariation,
                               OnCompleteType&& OnComplete);

    // Handle operations (C++)
    bool  IsTimerValid(const FEnhancedTimerHandle& Handle) const;
    void  InvalidateTimer(const FEnhancedTimerHandle& Handle);
    bool  IsTimerPaused(const FEnhancedTimerHandle& Handle) const;
    void  PauseTimer(const FEnhancedTimerHandle& Handle);
    void  UnpauseTimer(const FEnhancedTimerHandle& Handle);
    bool  IsTimerLooping(const FEnhancedTimerHandle& Handle) const;
    float GetTimerDuration(const FEnhancedTimerHandle& Handle) const;
    float GetTimerTimeLeft(const FEnhancedTimerHandle& Handle) const;
    float GetTimerElapsedTime(const FEnhancedTimerHandle& Handle) const;
    bool  IsTimerAffectedByGamePause(const FEnhancedTimerHandle& Handle) const;
    EEnhancedTimerTimeDilationMode GetTimerTimeDilationMode(const FEnhancedTimerHandle& Handle) const;

    // Bulk operations
    UFUNCTION(BlueprintCallable, Category="EnhancedTimers")
    void InvalidateAllTimers();

    UFUNCTION(BlueprintCallable, Category="EnhancedTimers")
    void PauseAllTimers();

    UFUNCTION(BlueprintCallable, Category="EnhancedTimers")
    void UnpauseAllTimers();

    // ========================= Blueprint API =========================

    UFUNCTION(BlueprintCallable, DisplayName="Set Enhanced Timer", Category="EnhancedTimers", meta=(WorldContext="WorldContextObject"))
    FEnhancedTimerHandle SetEnhancedTimer_BP(const UObject* WorldContextObject,
        const FTimerDynamicDelegate& Event,
        float Duration,
        EEnhancedTimerTimeDilationMode DilationMode = EEnhancedTimerTimeDilationMode::IgnoreTimeDilation,
        AActor* DilationActor = nullptr,
        bool bAffectedByGamePause = false,
        bool bLoop = false,
        float DelayToStartCountingDown = 0.f,
        float DelayToStartCountingDownVariation = 0.f);

    UFUNCTION(BlueprintCallable, DisplayName="Set Enhanced Timer Executed In Next Tick", Category="EnhancedTimers", meta=(WorldContext="WorldContextObject"))
    FEnhancedTimerHandle SetEnhancedTimerExecutedInNextTick_BP(const UObject* WorldContextObject,
        const FTimerDynamicDelegate& Event);

    UFUNCTION(BlueprintCallable, DisplayName="Is Timer Valid", Category="EnhancedTimers")
    bool IsTimerValid_BP(FEnhancedTimerHandle Handle) const { return IsTimerValid(Handle); }

    UFUNCTION(BlueprintCallable, DisplayName="Invalidate Timer", Category="EnhancedTimers")
    void InvalidateTimer_BP(FEnhancedTimerHandle Handle) { InvalidateTimer(Handle); }

    UFUNCTION(BlueprintCallable, DisplayName="Is Timer Paused", Category="EnhancedTimers")
    bool IsTimerPaused_BP(FEnhancedTimerHandle Handle) const { return IsTimerPaused(Handle); }

    UFUNCTION(BlueprintCallable, DisplayName="Pause Timer", Category="EnhancedTimers")
    void PauseTimer_BP(FEnhancedTimerHandle Handle) { PauseTimer(Handle); }

    UFUNCTION(BlueprintCallable, DisplayName="Unpause Timer", Category="EnhancedTimers")
    void UnpauseTimer_BP(FEnhancedTimerHandle Handle) { UnpauseTimer(Handle); }

    UFUNCTION(BlueprintPure, DisplayName="Is Timer Looping", Category="EnhancedTimers")
    bool IsTimerLooping_BP(FEnhancedTimerHandle Handle) const { return IsTimerLooping(Handle); }

    UFUNCTION(BlueprintPure, DisplayName="Get Timer Duration", Category="EnhancedTimers")
    float GetTimerDuration_BP(FEnhancedTimerHandle Handle) const { return GetTimerDuration(Handle); }

    UFUNCTION(BlueprintPure, DisplayName="Get Timer Time Left", Category="EnhancedTimers")
    float GetTimerTimeLeft_BP(FEnhancedTimerHandle Handle) const { return GetTimerTimeLeft(Handle); }

    UFUNCTION(BlueprintPure, DisplayName="Get Timer Elapsed Time", Category="EnhancedTimers")
    float GetTimerElapsedTime_BP(FEnhancedTimerHandle Handle) const { return GetTimerElapsedTime(Handle); }

    UFUNCTION(BlueprintPure, DisplayName="Is Timer Affected By Game Pause", Category="EnhancedTimers")
    bool IsTimerAffectedByGamePause_BP(FEnhancedTimerHandle Handle) const { return IsTimerAffectedByGamePause(Handle); }

    UFUNCTION(BlueprintPure, DisplayName="Get Timer Time Dilation Mode", Category="EnhancedTimers")
    EEnhancedTimerTimeDilationMode GetTimerTimeDilationMode_BP(FEnhancedTimerHandle Handle) const { return GetTimerTimeDilationMode(Handle); }

#if WITH_EDITOR || UE_BUILD_DEVELOPMENT
    UFUNCTION(CallInEditor, Category="EnhancedTimers|Debug")
    void DumpActiveTimers() const;
#endif

private:
    // Internal storage
    TMap<uint64, FEnhancedTimerData> Timers;
    TArray<uint64>                   FiredThisTick;     // to be executed this frame
    TArray<uint64>                   ToRemove;          // remove at end of frame
    TArray<uint64>                   ToUnpause;         // deferred unpause if needed
    uint64                           NextId = 1;

    // Reusable buffers to avoid per-tick allocations
    mutable TArray<uint64>                                   ReusableToFire;
    mutable TArray<TPair<uint64, FEnhancedTimerData>>        ReusableSnapshot;

    // Concurrency
    mutable FRWLock                  MapLock;

#if WITH_EDITOR || UE_BUILD_DEVELOPMENT
    mutable double                   LastTickTimeMs = 0.0;
    mutable int32                    TimersProcessedLastTick = 0;
#endif

    // Helpers
    uint64  AllocateId();
    bool    GetData(uint64 Id, FEnhancedTimerData& Out) const;
    FEnhancedTimerData* FindMutable(uint64 Id);
    void    ExecuteFired();
    void    Cleanup();

    void    EnforceGameThread() const;
    bool    IsGamePaused() const;
};

// ===== Inline template helper implementation =====
template<typename OnCompleteType>
void UEnhancedTimerManagerSubsystem::SetEnhancedTimerAsync(const FTimerDelegate& InDelegate,
                                                           float Duration,
                                                           EEnhancedTimerTimeDilationMode DilationMode,
                                                           AActor* DilationActor,
                                                           bool bAffectedByGamePause,
                                                           bool bLoop,
                                                           float DelayToStartCountingDown,
                                                           float DelayToStartCountingDownVariation,
                                                           OnCompleteType&& OnComplete)
{
    if (IsInGameThread())
    {
        FEnhancedTimerHandle H = SetEnhancedTimer(InDelegate, Duration, DilationMode, DilationActor,
                                                  bAffectedByGamePause, bLoop,
                                                  DelayToStartCountingDown, DelayToStartCountingDownVariation);
        OnComplete(H);
        return;
    }

    FTimerDelegate Copy = InDelegate;
    auto Complete = TUniqueFunction<void(FEnhancedTimerHandle)>(Forward<OnCompleteType>(OnComplete));

    AsyncTask(ENamedThreads::GameThread, [this, Copy, Duration, DilationMode, DilationActor,
                                          bAffectedByGamePause, bLoop,
                                          DelayToStartCountingDown, DelayToStartCountingDownVariation,
                                          Complete = MoveTemp(Complete)]() mutable
    {
        FEnhancedTimerHandle H = SetEnhancedTimer(Copy, Duration, DilationMode, DilationActor,
                                                  bAffectedByGamePause, bLoop,
                                                  DelayToStartCountingDown, DelayToStartCountingDownVariation);
        if (Complete) { Complete(MoveTemp(H)); }
    });
}
