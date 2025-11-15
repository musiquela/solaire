# JUCE Critical Production Knowledge

## THREADING - ABSOLUTELY CRITICAL

### prepareToPlay vs processBlock Race Condition
**FACT**: Some hosts (Nuendo, especially in DOP mode) WILL call `prepareToPlay()` while `processBlock()` is running on different threads.

**THE PROBLEM**:
- Calling `setLatencySamples()` in `prepareToPlay()` triggers `ComponentRestarter::handleAsyncUpdate()`
- This causes a SECOND `prepareToPlay()` call while audio is already processing
- If you're resizing buffers/filters in `prepareToPlay()`, you WILL crash

**SOLUTION**:
- Use spinlock (NOT std::lock_guard - that's a system call)
- Guard shared state between `prepareToPlay()` and `processBlock()`
- This is NOT theoretical - real commercial plugins crash without this

**CODE PATTERN**:
```cpp
// In header
juce::SpinLock prepareProcessLock;

// In prepareToPlay
{
    const juce::SpinLock::ScopedLockType lock(prepareProcessLock);
    // Setup filters, resize buffers, etc
    setLatencySamples(calculatedLatency);
}

// In processBlock
{
    const juce::SpinLock::ScopedTryLockType lock(prepareProcessLock);
    if (!lock.isLocked()) return; // Skip this buffer if preparing
    // Process audio
}
```

### parameterChanged IS THE AUDIO THREAD
**CRITICAL**: Both `AudioProcessorParameter::Listener::parameterChanged` and `parameterValueChanged` can be called from:
- Audio thread
- Message thread
- Any other thread

**NEVER DO**:
- `repaint()`
- Heavy allocations
- AsyncUpdater (it posts to message thread - not lock-free)
- Component::postCommandMessage

**PATTERN**:
```cpp
class MyComponent : public juce::Component, juce::Timer, juce::AudioProcessorParameter::Listener
{
    void timerCallback() override
    {
        if (needsRepaint.load())
        {
            needsRepaint = false;
            repaint();
        }
    }

    void parameterValueChanged(int, float) override
    {
        needsRepaint = true; // Atomic bool, picked up by timer
    }

private:
    std::atomic<bool> needsRepaint{false};
};
```

**MODERN**: Use `VBlankAttachment` (JUCE 7.0.6+) instead of Timer

### Audio Thread Rules
- NO allocations
- NO system calls
- NO locks (except spinlocks in extreme cases)
- NO DBG (it allocates)
- Avoid dereferencing hundreds of atomics in tight loops (performance killer)

## BUILD SYSTEM - CMAKE

### Critical Order
1. `juce_add_module` BEFORE `juce_add_plugin`
2. Target must exist BEFORE `target_link_libraries`
3. When in doubt: `rm -rf build`

### Speed Optimizations
```cmake
# Use Ninja generator (way faster)
cmake -B build -G Ninja

# Use all cores for configure
export CMAKE_BUILD_PARALLEL_LEVEL=8

# Build with parallel jobs
cmake --build build -j8
```

### Glob Usage
**SAFE NOW** (with CONFIGURE_DEPENDS):
```cmake
file(GLOB_RECURSE SourceFiles CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")
target_sources(MyPlugin PRIVATE ${SourceFiles})
```

### Variable Naming Hell
- `CMAKE_CURRENT_SOURCE_DIR` = current CMakeLists.txt directory
- `CMAKE_CURRENT_LIST_DIR` = current .cmake file directory (NOT CMakeLists.txt!)
- `CMAKE_SOURCE_DIR` = top-level CMakeLists.txt directory

### Windows Static Runtime
```cmake
if (WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
```

## DAW COMPATIBILITY NIGHTMARES

### macOS AU Registration
- AU MUST be in `/Library/Audio/Plugins/Components` or `~/Library/Audio/Plugins/Components`
- If not showing: `killall -9 AudioComponentRegistrar`
- Set version to 0 for debug builds = auto-scan every time

### AUv3 Registration
- Must **double-click standalone in Finder** to register AUv3
- Running from IDE won't register it

### Version Changes
- Use timestamp label to verify you're actually running new build
```cpp
label.setText(__DATE__ " " __TIME__ " " CMAKE_BUILD_TYPE, dontSendNotification);
```

### Plugin Format Differences
- VST3 and AU handle parameters differently
- VST3 IPluginCompatibility issues when transitioning from VST2
- DOP (Direct Offline Processing) creates new instances = more race conditions

## UI PERFORMANCE

### Component Drawing
- Component bounds = integers
- Drawing inside paint = floats
- Pixels in JUCE = logical (DPI-independent), NOT physical

### Pre-allocate Everything
```cpp
// GOOD - member variable
juce::Path backgroundPath;

void paint(Graphics& g) override
{
    g.fillPath(backgroundPath); // No allocation
}

// BAD - allocates every paint
void paint(Graphics& g) override
{
    juce::Path backgroundPath; // ALLOCATION
    g.fillPath(backgroundPath);
}
```

### LookAndFeel Destruction Order
```cpp
class MyComponent : public juce::Component
{
    // LookAndFeel FIRST (deleted last)
    CustomLookAndFeel laf;

    // Components using it SECOND (deleted first)
    juce::Slider slider;
};
```

### Repaint Debugging
```cmake
target_compile_definitions(MyPlugin PRIVATE JUCE_ENABLE_REPAINT_DEBUGGING=1)
```

## PRODUCTION SHIPPING

### pluginval is MANDATORY
- You WILL find 5+ bugs you didn't know existed
- Don't ship without it

### Avoid Singletons in Plugins
- DAWs may run multiple instances in same process OR different processes
- You can't predict which
- Singletons create ambiguity
- Pass state down through components instead

### Static Memory
- Don't use `static` in plugins
- Multiple instances in same process will share it
- `thread_local` won't help (hosts can run channel strip on one thread)

## DEBUGGING

### DBG Output
- Xcode: View > Debug Area > Activate Console
- DAW debugging: attach to process or enable "Debug Executable"
- Apple Silicon: requires disabling SIP, attach to `AUHostingServiceXPC_arrow`
- **DBG CAUSES DROPOUTS** in Debug builds (it allocates)

### Leak Detector
- Hit Continue to see ALL leaked types
- First leak might be member of last leak

### Build Types
- Debug = no optimizations, debug symbols
- Release = optimizations enabled
- RelWithDebInfo = best of both

## MISC CRITICAL TIPS

### Standalone First
- Fastest iteration (no DAW startup)
- Add to FORMATS in CMake even if not shipping
- Gracefully quit to save state (don't hit IDE stop)

### AudioPluginHost
- Save .filtergraph to preserve test setup
- Add EQ/metering to chain
- Will autoload last saved patch

### Custom Widgets
- Don't bother with LookAndFeel for your own components
- Just paint what you want in paint()
- LookAndFeels are for framework flexibility, not your app

### When You'll Spend Time
- 80% of plugin dev time = UI
- Not DSP, not logic - UI is the hard part
- Plan accordingly

## WHAT I NOW KNOW I DON'T KNOW
- Exact behavior of each plugin format wrapper implementation
- All edge cases in various DAW hosts
- Platform-specific audio device quirks
- Every CMake gotcha in complex module setups

This document represents REAL production knowledge from developers shipping commercial plugins.
