#include "Diagnostics.h"

#include <algorithm>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <cwchar>
#include <exception>

#if JUCE_MAC || JUCE_LINUX
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#elif JUCE_WINDOWS
#define NOMINMAX
#include <windows.h>
#endif

namespace megadsp
{
namespace
{
#if JUCE_MAC || JUCE_LINUX
constexpr std::array<int, 5> fatalSignals {
    SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV
};
std::array<struct sigaction, fatalSignals.size()> previousSignalActions {};
std::terminate_handler previousTerminateHandler = nullptr;
char crashFilePath[PATH_MAX] {};
volatile std::sig_atomic_t handlingFatalError = 0;
volatile std::sig_atomic_t crashEvent = 0;
volatile std::sig_atomic_t crashInstance = 0;
volatile std::sig_atomic_t crashValue = 0;

size_t appendUnsigned(
    char* destination, size_t position, size_t capacity,
    unsigned long long value)
{
    char reversed[24] {};
    size_t count = 0;
    do
    {
        reversed[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < sizeof(reversed));
    while (count > 0 && position < capacity)
        destination[position++] = reversed[--count];
    return position;
}

void writeCrashMarker(const char* reason, int signalNumber) noexcept
{
    const auto descriptor = ::open(
        crashFilePath, O_CREAT | O_WRONLY | O_APPEND, 0600);
    if (descriptor < 0)
        return;
    char line[192] {};
    size_t length = 0;
    const auto append = [&line, &length](const char* text)
    {
        while (*text != '\0' && length < sizeof(line) - 1)
            line[length++] = *text++;
    };
    append("fatal reason=");
    append(reason);
    append(" signal=");
    length = appendUnsigned(
        line, length, sizeof(line) - 1,
        static_cast<unsigned long long>(signalNumber));
    append(" pid=");
    length = appendUnsigned(
        line, length, sizeof(line) - 1,
        static_cast<unsigned long long>(::getpid()));
    append(" event=");
    length = appendUnsigned(
        line, length, sizeof(line) - 1,
        static_cast<unsigned long long>(crashEvent));
    append(" instance=");
    length = appendUnsigned(
        line, length, sizeof(line) - 1,
        static_cast<unsigned long long>(crashInstance));
    append(" value=");
    length = appendUnsigned(
        line, length, sizeof(line) - 1,
        static_cast<unsigned long long>(crashValue));
    append("\n");
    static_cast<void>(::write(descriptor, line, length));
    ::close(descriptor);
}

[[maybe_unused]] void fatalSignalHandler(int signalNumber) noexcept
{
    if (handlingFatalError != 0)
        _exit(128 + signalNumber);
    handlingFatalError = 1;
    writeCrashMarker("signal", signalNumber);

    for (size_t index = 0; index < fatalSignals.size(); ++index)
        if (fatalSignals[index] == signalNumber)
        {
            ::sigaction(
                signalNumber, &previousSignalActions[index], nullptr);
            break;
        }
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, signalNumber);
    sigprocmask(SIG_UNBLOCK, &signals, nullptr);
    ::kill(::getpid(), signalNumber);
    _exit(128 + signalNumber);
}

[[maybe_unused]] void terminateHandler() noexcept
{
    writeCrashMarker("terminate", 0);
    if (previousTerminateHandler != nullptr
        && previousTerminateHandler != terminateHandler)
        previousTerminateHandler();
    std::abort();
}
#elif JUCE_WINDOWS
std::terminate_handler previousTerminateHandler = nullptr;
wchar_t crashFilePath[MAX_PATH] {};
PVOID vectoredHandlerHandle = nullptr;
volatile long crashEvent = 0;
volatile long crashInstance = 0;
volatile long crashValue = 0;

void writeCrashMarker(const char* reason, unsigned long exceptionCode) noexcept
{
    const auto file = ::CreateFileW(
        crashFilePath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    char line[192] {};
    const auto length = std::snprintf(
        line, sizeof(line),
        "fatal reason=%s exception=%lu pid=%lu event=%ld instance=%ld value=%ld\n",
        reason, exceptionCode, ::GetCurrentProcessId(),
        crashEvent, crashInstance, crashValue);
    DWORD written = 0;
    if (length > 0)
        ::WriteFile(
            file, line,
            static_cast<DWORD>(juce::jmin<int>(
                length, static_cast<int>(sizeof(line) - 1))),
            &written, nullptr);
    ::CloseHandle(file);
}

[[maybe_unused]] LONG CALLBACK vectoredExceptionHandler(
    EXCEPTION_POINTERS* exception) noexcept
{
    const auto code = exception != nullptr
        && exception->ExceptionRecord != nullptr
        ? exception->ExceptionRecord->ExceptionCode : 0;
    writeCrashMarker("exception", code);
    return EXCEPTION_CONTINUE_SEARCH;
}

[[maybe_unused]] void terminateHandler() noexcept
{
    writeCrashMarker("terminate", 0);
    if (previousTerminateHandler != nullptr
        && previousTerminateHandler != terminateHandler)
        previousTerminateHandler();
    std::abort();
}
#endif

juce::String eventName(std::uint32_t event)
{
    switch (static_cast<DiagnosticEvent>(event))
    {
        case DiagnosticEvent::audioConfiguration: return "audio-config";
        case DiagnosticEvent::latencyChanged: return "latency";
        case DiagnosticEvent::topologyChanged: return "topology";
        case DiagnosticEvent::stateRejected: return "state-rejected";
        case DiagnosticEvent::impulseResponseFailed: return "ir-failed";
    }
    return "message";
}
} // namespace

Diagnostics& Diagnostics::instance()
{
    static Diagnostics diagnostics;
    return diagnostics;
}

Diagnostics::Diagnostics()
    : Thread("megaDSP diagnostics")
{
#if !defined(MEGADSP_TESTS)
    if (!folder().getChildFile("diagnostics.disabled").existsAsFile())
        setEnabled(true);
#endif
}

Diagnostics::~Diagnostics()
{
    stop(false);
}

juce::File Diagnostics::folder() const
{
#if defined(MEGADSP_TESTS)
    return juce::File::getSpecialLocation(
               juce::File::tempDirectory)
        .getChildFile("megaDSPTests-Diagnostics");
#else
    return juce::File::getSpecialLocation(
               juce::File::userApplicationDataDirectory)
        .getChildFile("megaDSP")
        .getChildFile("Diagnostics");
#endif
}

void Diagnostics::openFolder() const
{
    const auto directory = folder();
    directory.createDirectory();
    directory.startAsProcess();
}

void Diagnostics::setEnabled(bool shouldEnable)
{
    if (shouldEnable == isEnabled())
        return;
    const auto directory = folder();
    if (shouldEnable)
    {
        if (directory.createDirectory().failed())
            return;
        directory.getChildFile("diagnostics.disabled").deleteFile();
        directory.getChildFile("diagnostics.enabled")
            .replaceWithText("megaDSP diagnostics enabled\n");
#if JUCE_MAC || JUCE_LINUX
        const auto path =
            directory.getChildFile("megaDSP-crash.txt").getFullPathName();
        std::strncpy(
            crashFilePath, path.toRawUTF8(), sizeof(crashFilePath) - 1);
#elif JUCE_WINDOWS
        const auto path =
            directory.getChildFile("megaDSP-crash.txt").getFullPathName();
        std::wcsncpy(
            crashFilePath, path.toWideCharPointer(), MAX_PATH - 1);
#endif
        enabled.store(true, std::memory_order_release);
        installCrashHandlers();
        startThread(juce::Thread::Priority::low);
        log("diagnostics", "enabled");
    }
    else
        stop(true);
}

void Diagnostics::stop(bool rememberDisabled)
{
    if (!isEnabled())
        return;
    log("diagnostics", rememberDisabled ? "disabled" : "shutdown");
    enabled.store(false, std::memory_order_release);
    stopThread(1000);
    drain();
    output.reset();
    uninstallCrashHandlers();
    if (!rememberDisabled)
        return;
    const auto directory = folder();
    if (directory.createDirectory().wasOk())
        directory.getChildFile("diagnostics.disabled")
            .replaceWithText("megaDSP diagnostics disabled\n");
    directory.getChildFile("diagnostics.enabled").deleteFile();
}

std::uint32_t Diagnostics::registerInstance()
{
    const auto identifier =
        nextInstanceId.fetch_add(1, std::memory_order_relaxed);
    log("instance", "created id=" + juce::String(identifier));
    return identifier;
}

void Diagnostics::unregisterInstance(std::uint32_t identifier)
{
    log("instance", "destroyed id=" + juce::String(identifier));
}

void Diagnostics::log(const char* category, const juce::String& message)
{
    if (!isEnabled())
        return;
    const auto text = juce::String(category) + " " + message;
    enqueue(0, 0, 0, 0, text.toRawUTF8());
}

void Diagnostics::logRealtime(
    DiagnosticEvent event, std::uint32_t instanceId,
    std::int64_t valueA, std::int64_t valueB) noexcept
{
    if (!isEnabled())
        return;
    updateCrashContext(event, instanceId, valueA);
    enqueue(
        static_cast<std::uint32_t>(event), instanceId,
        valueA, valueB, nullptr);
}

void Diagnostics::updateCrashContext(
    DiagnosticEvent event, std::uint32_t instanceId,
    std::int64_t valueA) noexcept
{
#if JUCE_MAC || JUCE_LINUX
    crashEvent = static_cast<std::sig_atomic_t>(event);
    crashInstance = static_cast<std::sig_atomic_t>(instanceId);
    crashValue = static_cast<std::sig_atomic_t>(valueA);
#elif JUCE_WINDOWS
    crashEvent = static_cast<long>(event);
    crashInstance = static_cast<long>(instanceId);
    crashValue = static_cast<long>(valueA);
#else
    juce::ignoreUnused(event, instanceId, valueA);
#endif
}

bool Diagnostics::enqueue(
    std::uint32_t event, std::uint32_t instanceId,
    std::int64_t valueA, std::int64_t valueB,
    const char* text) noexcept
{
    auto& entry = queue[static_cast<size_t>(
        writeSequence.fetch_add(1, std::memory_order_relaxed) % queueSize)];
    auto expected = 0;
    if (!entry.state.compare_exchange_strong(
            expected, 1, std::memory_order_acquire))
    {
        droppedEntries.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    entry.ticks = static_cast<std::uint64_t>(
        juce::Time::currentTimeMillis());
    entry.event = event;
    entry.instanceId = instanceId;
    entry.valueA = valueA;
    entry.valueB = valueB;
    entry.text.fill('\0');
    if (text != nullptr)
        std::strncpy(entry.text.data(), text, entry.text.size() - 1);
    entry.state.store(2, std::memory_order_release);
    return true;
}

void Diagnostics::run()
{
    writeHeader();
    while (!threadShouldExit())
    {
        drain();
        wait(200);
    }
    drain();
}

void Diagnostics::writeHeader()
{
    rotateIfNeeded();
    const auto logFile = folder().getChildFile("megaDSP.log");
    const auto appendPosition = logFile.getSize();
    output = logFile.createOutputStream();
    if (output == nullptr)
        return;
    output->setPosition(appendPosition);
    output->writeText(
        "\n=== megaDSP " + juce::String(MEGADSP_VERSION)
            + " diagnostics session " + juce::Time::getCurrentTime().toISO8601(true)
            + " ===\nOS: " + juce::SystemStats::getOperatingSystemName()
            + "\nJUCE: " + juce::SystemStats::getJUCEVersion()
            + "\nCPU: " + juce::SystemStats::getCpuModel()
            + "\nAttach this log, megaDSP-crash.txt, and the DAW/OS crash report "
              "when filing an issue.\n",
        false, false, "\n");
    output->flush();
}

void Diagnostics::rotateIfNeeded()
{
    auto log = folder().getChildFile("megaDSP.log");
    if (!log.existsAsFile() || log.getSize() < 1024 * 1024)
        return;
    folder().getChildFile("megaDSP.3.log").deleteFile();
    folder().getChildFile("megaDSP.2.log")
        .moveFileTo(folder().getChildFile("megaDSP.3.log"));
    folder().getChildFile("megaDSP.1.log")
        .moveFileTo(folder().getChildFile("megaDSP.2.log"));
    log.moveFileTo(folder().getChildFile("megaDSP.1.log"));
}

void Diagnostics::drain()
{
    if (output == nullptr && isEnabled())
        writeHeader();
    if (output == nullptr)
        return;
    std::array<Entry*, queueSize> ready {};
    size_t readyCount = 0;
    for (auto& entry : queue)
    {
        auto expected = 2;
        if (!entry.state.compare_exchange_strong(
                expected, 3, std::memory_order_acquire))
            continue;
        ready[readyCount++] = &entry;
    }
    std::sort(
        ready.begin(), ready.begin() + static_cast<std::ptrdiff_t>(readyCount),
        [](const Entry* left, const Entry* right)
        {
            return left->ticks < right->ticks;
        });
    for (size_t index = 0; index < readyCount; ++index)
    {
        auto& entry = *ready[index];
        juce::String line;
        if (entry.event == 0)
            line = entry.text.data();
        else
            line = eventName(entry.event)
                + " instance=" + juce::String(entry.instanceId)
                + " a=" + juce::String(entry.valueA)
                + " b=" + juce::String(entry.valueB);
        output->writeText(
            juce::Time(static_cast<juce::int64>(entry.ticks)).toISO8601(true)
                + " " + line + "\n",
            false, false, "\n");
        entry.state.store(0, std::memory_order_release);
    }
    const auto dropped = droppedEntries.exchange(0);
    if (dropped != 0)
        output->writeText(
            "diagnostics dropped=" + juce::String(dropped) + "\n",
            false, false, "\n");
    output->flush();
    if (output->getPosition() >= 1024 * 1024)
    {
        output.reset();
        writeHeader();
    }
}

void Diagnostics::installCrashHandlers()
{
#if defined(MEGADSP_TESTS)
    return;
#elif JUCE_MAC || JUCE_LINUX
    struct sigaction action {};
    action.sa_handler = fatalSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    for (size_t index = 0; index < fatalSignals.size(); ++index)
        ::sigaction(
            fatalSignals[index], &action, &previousSignalActions[index]);
    previousTerminateHandler = std::set_terminate(terminateHandler);
#elif JUCE_WINDOWS
    vectoredHandlerHandle =
        ::AddVectoredExceptionHandler(0, vectoredExceptionHandler);
    previousTerminateHandler = std::set_terminate(terminateHandler);
#endif
}

void Diagnostics::uninstallCrashHandlers()
{
#if defined(MEGADSP_TESTS)
    return;
#elif JUCE_MAC || JUCE_LINUX
    for (size_t index = 0; index < fatalSignals.size(); ++index)
    {
        struct sigaction current {};
        ::sigaction(fatalSignals[index], nullptr, &current);
        if (current.sa_handler == fatalSignalHandler)
            ::sigaction(
                fatalSignals[index], &previousSignalActions[index], nullptr);
    }
    if (std::get_terminate() == terminateHandler)
        std::set_terminate(previousTerminateHandler);
#elif JUCE_WINDOWS
    if (vectoredHandlerHandle != nullptr)
    {
        ::RemoveVectoredExceptionHandler(vectoredHandlerHandle);
        vectoredHandlerHandle = nullptr;
    }
    if (std::get_terminate() == terminateHandler)
        std::set_terminate(previousTerminateHandler);
#endif
}
} // namespace megadsp
