# Enhanced Timer Manager

*Read this in other languages: [English](#enhanced-timer-manager-english), [Turkish](#enhanced-timer-manager-türkçe)*

## Enhanced Timer Manager (English)

This system provides a high-performance, flexible timer management solution for Unreal Engine. It extends the standard timer functionality with features like time-dilation awareness, game-pause handling, initial delay variations, and a thread-safe API, making it ideal for complex gameplay mechanics, UI animations, and asynchronous operations.

### Features

  - **Time-Dilation Awareness**: Timers can be configured to respect global time dilation, a specific actor's time dilation, or ignore it completely.
  - **Game Pause Handling**: Choose whether a timer should continue to update when the game is paused.
  - **Initial Delay**: Set a timer with an initial delay before it starts its countdown, with an optional random variation.
  - **Thread-Safe API**: Public functions can be called from any thread; calls are automatically marshalled to the Game Thread.
  - **Performance Optimized**: Designed to be highly efficient by using pre-allocated, reusable containers to avoid memory allocations during the tick phase.
  - **Blueprint & C++ API**: Provides a comprehensive and consistent API for both C++ and Blueprints.
  - **Next-Tick Execution**: Easily schedule a delegate to be executed on the very next frame.
  - **Detailed Control**: Each timer is managed via a `FEnhancedTimerHandle`, allowing for individual control (pause, unpause, invalidate, query state).

### Installation

1.  Copy the `EnhancedTimerManager` module to your project's `Source` directory.
2.  Add the module dependency to your project's `.Build.cs` file:
    ```csharp
    PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "EnhancedTimerManager" });
    ```
3.  Rebuild your project.
4.  The `UEnhancedTimerManagerSubsystem` will be available as a `GameInstanceSubsystem`.

### Usage

#### Basic C++ Usage

```cpp
// In any class, e.g., an Actor's BeginPlay
void AMyActor::BeginPlay()
{
    Super::BeginPlay();

    // Get the Enhanced Timer Manager Subsystem
    UEnhancedTimerManagerSubsystem* TimerSystem = GetGameInstance()->GetSubsystem<UEnhancedTimerManagerSubsystem>();
    if (!TimerSystem)
        return;

    // Set a simple looping timer that fires every 2 seconds
    FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &AMyActor::MyRepeatingFunction);
    TimerSystem->SetEnhancedTimer(Delegate, 2.0f, EEnhancedTimerTimeDilationMode::IgnoreTimeDilation, nullptr, false, /*bLoop=*/true);
}

void AMyActor::MyRepeatingFunction()
{
    UE_LOG(LogTemp, Log, TEXT("Repeating timer fired!"));
}
```

#### Advanced C++ Usage

```cpp
void AMyPlayerController::SetupAbilityCooldown()
{
    UEnhancedTimerManagerSubsystem* TimerSystem = GetGameInstance()->GetSubsystem<UEnhancedTimerManagerSubsystem>();
    if (!TimerSystem)
        return;

    // Set a one-shot timer with a 5-second duration and an initial delay of 1 to 1.5 seconds.
    // This timer will be affected by the player character's custom time dilation.
    FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &AMyPlayerController::OnCooldownFinished);
    float Duration = 5.0f;
    AActor* DilationActor = GetCharacter();
    bool bAffectedByGamePause = false;
    bool bLoop = false;
    float Delay = 1.0f;
    float DelayVariation = 0.5f;

    FEnhancedTimerHandle CooldownHandle = TimerSystem->SetEnhancedTimer(
        Delegate, Duration, EEnhancedTimerTimeDilationMode::ActorTimeDilation, DilationActor,
        bAffectedByGamePause, bLoop, Delay, DelayVariation
    );

    // Later, you can interact with the timer using its handle
    if (CooldownHandle.IsValid())
    {
        float TimeLeft = CooldownHandle.GetTimeLeft();
        UE_LOG(LogTemp, Log, TEXT("Cooldown time left: %f"), TimeLeft);
        CooldownHandle.Pause();
    }
}
```

#### Blueprint Usage

You can also use the system from Blueprints:

1.  Get the Enhanced Timer Manager Subsystem:
    *Get Game Instance -\> Get Subsystem (Class: Enhanced Timer Manager Subsystem)*

2.  Set a timer and store its handle:
    *Call the "Set Enhanced Timer" node with the desired parameters and promote the return value to a variable.*

3.  Use the handle to control the timer:
    *Use the handle variable to call functions like "Pause Timer", "Get Time Left", or "Invalidate Timer".*

### Time Dilation Modes

The `EEnhancedTimerTimeDilationMode` enum allows you to control how a timer is affected by time:

  - **IgnoreTimeDilation**: The timer runs in real-time, ignoring all global and actor-specific time dilation.
  - **GlobalTimeDilation**: The timer's speed is scaled by the global time dilation (`UGameplayStatics::GetGlobalTimeDilation`).
  - **ActorTimeDilation**: The timer's speed is scaled by the `CustomTimeDilation` of a specific actor. If the actor becomes invalid, it falls back to `IgnoreTimeDilation`.

### Performance Recommendations

  - The system is already highly optimized. Feel free to use it for high-frequency or long-running timers.
  - For timers created outside the Game Thread, use `SetEnhancedTimerAsync` to get the handle back in a callback without blocking the calling thread.
  - Store the `FEnhancedTimerHandle` returned by `SetEnhancedTimer` to manage the timer's lifecycle (pausing, unpausing, or invalidating it early).
  - Avoid creating extremely short-lived timers in a tight loop every frame. For per-frame logic, a standard `Tick` function is still more appropriate.

### Technical Details

#### Thread Safety and Concurrency

All public API functions (`SetEnhancedTimer`, `PauseTimer`, etc.) perform a check to see if they are being called on the Game Thread. If called from an outside thread, the operation is automatically marshalled to the Game Thread using `AsyncTask`. This ensures that the core timer map is only ever accessed on the Game Thread, preventing race conditions. Internally, the timer map is further protected by an `FRWLock` for safe read/write access during the `Tick` phase, although with the marshalling strategy, this primarily protects against edge cases.

#### Performance and Memory

The core `Tick` function is designed to avoid heap allocations. It uses pre-allocated `TArray` members (`ReusableSnapshot`, `ReusableToFire`) as reusable buffers. During each tick, it first creates a snapshot of all active timers, then iterates over this snapshot to update timer states and identify which ones should fire. This avoids issues with modifying the main timer map while iterating over it and minimizes per-tick memory overhead, which is critical for maintaining stable performance.

#### Statistics and Debugging

In `Development` or `Editor` builds, you can call `DumpActiveTimers()` to log a detailed list of all active timers and their current states, along with performance metrics from the last tick.

```cpp
#if WITH_EDITOR || UE_BUILD_DEVELOPMENT
    TimerSystem->DumpActiveTimers();
#endif
```

-----

## Enhanced Timer Manager (Türkçe)

Bu sistem, Unreal Engine için yüksek performanslı ve esnek bir zamanlayıcı (timer) yönetimi çözümü sunar. Standart zamanlayıcı işlevselliğini, zaman yavaşlatma (time-dilation) farkındalığı, oyun duraklatma yönetimi, başlangıç gecikmesi varyasyonları ve thread-güvenli bir API gibi özelliklerle genişletir. Bu da onu karmaşık oyun mekanikleri, UI animasyonları ve asenkron operasyonlar için ideal kılar.

### Özellikler

  - **Time-Dilation Farkındalığı**: Zamanlayıcılar, global zaman yavaşlamasına, belirli bir aktörün zaman yavaşlamasına saygı duyacak veya tamamen görmezden gelecek şekilde yapılandırılabilir.
  - **Oyun Duraklatma Yönetimi**: Bir zamanlayıcının oyun duraklatıldığında güncellenmeye devam edip etmeyeceğini seçin.
  - **Başlangıç Gecikmesi**: Geri sayımına başlamadan önce bir başlangıç gecikmesiyle (isteğe bağlı rastgele bir varyasyonla) bir zamanlayıcı ayarlayın.
  - **Thread-Güvenli API**: Public fonksiyonlar herhangi bir thread'den çağrılabilir; çağrılar otomatik olarak Game Thread'e yönlendirilir.
  - **Performans Optimizasyonu**: Tick aşamasında bellek ayırmalarını (memory allocation) önlemek için önceden boyutlandırılmış, yeniden kullanılabilir konteynerler kullanarak yüksek verimli olacak şekilde tasarlanmıştır.
  - **Blueprint & C++ API**: Hem C++ hem de Blueprint'ler için kapsamlı ve tutarlı bir API sağlar.
  - **Sonraki-Tick'te Çalıştırma**: Bir delegenin bir sonraki frame'de çalıştırılmasını kolayca zamanlayın.
  - **Detaylı Kontrol**: Her zamanlayıcı, bireysel kontrole (durdurma, devam ettirme, geçersiz kılma, durum sorgulama) olanak tanıyan bir `FEnhancedTimerHandle` aracılığıyla yönetilir.

### Kurulum

1.  `EnhancedTimerManager` modülünü projenizin `Source` dizinine kopyalayın.
2.  Projenizin `.Build.cs` dosyasına modül bağımlılığını ekleyin:
    ```csharp
    PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "EnhancedTimerManager" });
    ```
3.  Projenizi yeniden derleyin.
4.  `UEnhancedTimerManagerSubsystem`, bir `GameInstanceSubsystem` olarak kullanılabilir olacaktır.

### Kullanım

#### Temel C++ Kullanımı

```cpp
// Herhangi bir sınıfta, örneğin bir Actor'un BeginPlay'inde
void AMyActor::BeginPlay()
{
    Super::BeginPlay();

    // Enhanced Timer Manager Subsystem'ini al
    UEnhancedTimerManagerSubsystem* TimerSystem = GetGameInstance()->GetSubsystem<UEnhancedTimerManagerSubsystem>();
    if (!TimerSystem)
        return;

    // Her 2 saniyede bir tetiklenen basit bir döngüsel zamanlayıcı ayarla
    FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &AMyActor::MyRepeatingFunction);
    TimerSystem->SetEnhancedTimer(Delegate, 2.0f, EEnhancedTimerTimeDilationMode::IgnoreTimeDilation, nullptr, false, /*bLoop=*/true);
}

void AMyActor::MyRepeatingFunction()
{
    UE_LOG(LogTemp, Log, TEXT("Tekrarlayan zamanlayıcı tetiklendi!"));
}
```

#### İleri Düzey C++ Kullanımı

```cpp
void AMyPlayerController::SetupAbilityCooldown()
{
    UEnhancedTimerManagerSubsystem* TimerSystem = GetGameInstance()->GetSubsystem<UEnhancedTimerManagerSubsystem>();
    if (!TimerSystem)
        return;

    // 5 saniye süreli ve 1 ila 1.5 saniye arasında bir başlangıç gecikmesi olan tek seferlik bir zamanlayıcı ayarla.
    // Bu zamanlayıcı, oyuncu karakterinin custom time dilation'ından etkilenecektir.
    FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &AMyPlayerController::OnCooldownFinished);
    float Duration = 5.0f;
    AActor* DilationActor = GetCharacter();
    bool bAffectedByGamePause = false;
    bool bLoop = false;
    float Delay = 1.0f;
    float DelayVariation = 0.5f;

    FEnhancedTimerHandle CooldownHandle = TimerSystem->SetEnhancedTimer(
        Delegate, Duration, EEnhancedTimerTimeDilationMode::ActorTimeDilation, DilationActor,
        bAffectedByGamePause, bLoop, Delay, DelayVariation
    );

    // Daha sonra, handle'ını kullanarak zamanlayıcı ile etkileşime girebilirsiniz
    if (CooldownHandle.IsValid())
    {
        float TimeLeft = CooldownHandle.GetTimeLeft();
        UE_LOG(LogTemp, Log, TEXT("Cooldown kalan süre: %f"), TimeLeft);
        CooldownHandle.Pause();
    }
}
```

#### Blueprint Kullanımı

Sistemi Blueprint'lerden de kullanabilirsiniz:

1.  Enhanced Timer Manager Subsystem'ini alın:
    *Get Game Instance -\> Get Subsystem (Sınıf: Enhanced Timer Manager Subsystem)*

2.  Bir zamanlayıcı ayarlayın ve handle'ını saklayın:
    *"Set Enhanced Timer" node'unu istediğiniz parametrelerle çağırın ve geri dönüş değerini bir değişkene yükseltin.*

3.  Zamanlayıcıyı kontrol etmek için handle'ı kullanın:
    *Handle değişkenini kullanarak "Pause Timer", "Get Time Left" veya "Invalidate Timer" gibi fonksiyonları çağırın.*

### Time Dilation Modları

`EEnhancedTimerTimeDilationMode` enum'u, bir zamanlayıcının zamandan nasıl etkilendiğini kontrol etmenizi sağlar:

  - **IgnoreTimeDilation**: Zamanlayıcı, tüm global ve aktöre özgü zaman yavaşlamalarını göz ardı ederek gerçek zamanlı çalışır.
  - **GlobalTimeDilation**: Zamanlayıcının hızı, global zaman yavaşlaması (`UGameplayStatics::GetGlobalTimeDilation`) ile ölçeklenir.
  - **ActorTimeDilation**: Zamanlayıcının hızı, belirli bir aktörün `CustomTimeDilation`'ı ile ölçeklenir. Eğer aktör geçersiz hale gelirse, `IgnoreTimeDilation` moduna geri döner.

### Performans Önerileri

  - Sistem zaten yüksek düzeyde optimize edilmiştir. Yüksek frekanslı veya uzun süreli zamanlayıcılar için kullanmaktan çekinmeyin.
  - Game Thread dışında oluşturulan zamanlayıcılar için, çağıran thread'i engellemeden handle'ı bir callback içinde geri almak için `SetEnhancedTimerAsync` kullanın.
  - Zamanlayıcının yaşam döngüsünü yönetmek (durdurmak, devam ettirmek veya erken geçersiz kılmak) için `SetEnhancedTimer` tarafından döndürülen `FEnhancedTimerHandle`'ı saklayın.
  - Her frame'de sıkışık bir döngü içinde aşırı kısa ömürlü zamanlayıcılar oluşturmaktan kaçının. Frame başına mantık için, standart bir `Tick` fonksiyonu hala daha uygundur.

### Teknik Detaylar

#### Thread Güvenliği ve Eşzamanlılık

Tüm public API fonksiyonları (`SetEnhancedTimer`, `PauseTimer`, vb.), Game Thread üzerinde çağrılıp çağrılmadıklarını kontrol eder. Eğer dış bir thread'den çağrılırlarsa, işlem `AsyncTask` kullanılarak otomatik olarak Game Thread'e yönlendirilir. Bu, ana zamanlayıcı haritasına yalnızca Game Thread üzerinde erişilmesini sağlayarak race condition'ları önler. Dahili olarak, zamanlayıcı haritası ayrıca `Tick` aşamasında güvenli okuma/yazma erişimi için bir `FRWLock` ile korunur, ancak yönlendirme stratejisiyle bu öncelikle istisnai durumları önler.

#### Performans ve Bellek

Çekirdek `Tick` fonksiyonu, heap ayırmalarından kaçınmak için tasarlanmıştır. Yeniden kullanılabilir tamponlar olarak önceden ayrılmış `TArray` üyelerini (`ReusableSnapshot`, `ReusableToFire`) kullanır. Her tick sırasında, önce tüm aktif zamanlayıcıların bir anlık görüntüsünü oluşturur, ardından zamanlayıcı durumlarını güncellemek ve hangilerinin tetiklenmesi gerektiğini belirlemek için bu anlık görüntü üzerinde yinelenir. Bu, ana zamanlayıcı haritasını üzerinde yinelenirken değiştirme sorunlarını önler ve kararlı performansı korumak için kritik olan tick başına bellek yükünü en aza indirir.

#### İstatistikler ve Hata Ayıklama

`Development` veya `Editor` build'lerinde, tüm aktif zamanlayıcıların ve mevcut durumlarının ayrıntılı bir listesini ve son tick'ten performans metriklerini loglamak için `DumpActiveTimers()` fonksiyonunu çağırabilirsiniz.

```cpp
#if WITH_EDITOR || UE_BUILD_DEVELOPMENT
    TimerSystem->DumpActiveTimers();
#endif
```