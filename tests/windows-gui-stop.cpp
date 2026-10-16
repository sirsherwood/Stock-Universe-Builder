// A real Win32 process/window smoke test; no credentials or network are used.
#include "../GUI/StockUniverseBuilderGui.cpp"

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Predicate>
void pumpUntil(Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done()) {
        require(std::chrono::steady_clock::now() < deadline, "Timed out waiting for GUI/core");
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
}

void checkIdle(const wchar_t* status) {
    require(!g_jobRunning && !g_activeJob, "Job still active");
    require(controlText(g_jobStatus) == status, "Wrong terminal status");
    require(IsWindowEnabled(g_pullButton) && IsWindowEnabled(g_refreshButton) &&
                IsWindowEnabled(g_credentialButton), "Job controls not restored");
    require(!IsWindowEnabled(g_stopButton), "Stop enabled while idle");
}

void clickStop() {
    SendMessageW(g_mainWindow, WM_COMMAND, MAKEWPARAM(IdStopButton, BN_CLICKED), 0);
}

void startFixture(JobKind kind, const wchar_t* ticket, bool continueWithPull = false) {
    launchJob(kind, applicationPath(ticket), continueWithPull, false);
    require(g_jobRunning && IsWindowEnabled(g_stopButton), "Job did not start");
    require(!IsWindowEnabled(g_pullButton) && !IsWindowEnabled(g_refreshButton) &&
                !IsWindowEnabled(g_credentialButton), "Job controls enabled during run");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    // The test executable doubles as the core in an isolated temporary directory.
    if (argc > 1) {
        if (std::wstring(argv[1]) == L"complete.ticket") return 0;
        std::printf("[1/10] Fixture running\n");
        std::fflush(stdout);
        for (;;) Sleep(1000);
    }

    std::filesystem::path testDirectory;
    try {
        wchar_t temporary[MAX_PATH]{};
        require(GetTempPathW(MAX_PATH, temporary) != 0, "Could not locate temp directory");
        testDirectory = std::filesystem::path(temporary) /
            (L"Stock GUI Stop Test " + std::to_wstring(GetCurrentProcessId()) + L" " +
             std::to_wstring(GetTickCount64()));
        require(std::filesystem::create_directory(testDirectory), "Could not create test directory");
        std::vector<wchar_t> self(32768);
        require(GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size())) != 0,
                "Could not locate test executable");
        std::filesystem::copy_file(self.data(), testDirectory / kCoreExecutable);
        g_applicationDirectory = testDirectory;
        std::filesystem::create_directory(applicationPath(L"Data"));
        std::wstring error;
        require(writeUtf8File(applicationPath(L"Data/existing.csv"), "existing output\n", error),
                "Could not write output fixture");
        g_instance = GetModuleHandleW(nullptr);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_DATE_CLASSES | ICC_PROGRESS_CLASS};
        require(InitCommonControlsEx(&controls) && registerWindowClasses(), "Window setup failed");
        g_mainWindow = CreateWindowExW(0, kMainWindowClass, L"Stop test", WS_OVERLAPPED,
                                      0, 0, 880, 740, nullptr, nullptr, g_instance, nullptr);
        require(g_mainWindow != nullptr, "Window creation failed");
        require(!IsWindowEnabled(g_stopButton), "Stop enabled before any job");
        appendLog(L"Existing log output\r\n");

        // Terminate a live process while the reader is blocked on its output pipe.
        startFixture(JobKind::PullData, L"running.ticket");
        pumpUntil([] { return controlText(g_log).find(L"Fixture running") != std::wstring::npos; });
        HANDLE observedProcess = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_activeJob->mutex);
            require(DuplicateHandle(GetCurrentProcess(), g_activeJob->process, GetCurrentProcess(),
                                    &observedProcess, SYNCHRONIZE, FALSE, 0),
                    "Could not observe core process");
        }
        clickStop();
        require(controlText(g_jobStatus) == L"Job status: Stopping", "Missing Stopping state");
        require(!IsWindowEnabled(g_stopButton) && !IsWindowEnabled(g_pullButton),
                "Controls restored before process exit");
        clickStop(); // Repeated requests must be harmless.
        pumpUntil([] { return !g_jobRunning; });
        checkIdle(L"Job status: Stopped");
        require(WaitForSingleObject(observedProcess, 0) == WAIT_OBJECT_0, "Core still alive");
        CloseHandle(observedProcess);
        require(controlText(g_log).find(L"Existing log output") != std::wstring::npos &&
                    controlText(g_log).find(L"Fixture running") != std::wstring::npos,
                "Existing log output lost");
        std::ifstream existing(applicationPath(L"Data/existing.csv"), std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(existing)), {});
        require(contents == "existing output\n", "Existing output file changed");
        existing.close();

        // Stop immediately, before launch/handle publication may have completed.
        startFixture(JobKind::PullData, L"running.ticket");
        clickStop();
        pumpUntil([] { return !g_jobRunning; });
        checkIdle(L"Job status: Stopped");

        // Stop an active refresh that would otherwise start a pull.
        startFixture(JobKind::RefreshStockList, L"running.ticket", true);
        clickStop();
        pumpUntil([] { return !g_jobRunning; });
        checkIdle(L"Job status: Stopped");

        // Stop after refresh exits but before its completion message is handled.
        startFixture(JobKind::RefreshStockList, L"complete.ticket", true);
        const ULONGLONG deadline = GetTickCount64() + 10000;
        MSG finished{};
        while (!PeekMessageW(&finished, g_mainWindow, kMessageJobFinished,
                             kMessageJobFinished, PM_NOREMOVE)) {
            require(GetTickCount64() < deadline, "Refresh did not complete");
            Sleep(10);
        }
        clickStop();
        pumpUntil([] { return !g_jobRunning; });
        checkIdle(L"Job status: Stopped");
        require(controlText(g_log).find(L"Continuing with the data pull") == std::wstring::npos,
                "Stopped refresh started a pull");

        // A fresh job must still complete normally after cancellation.
        startFixture(JobKind::PullData, L"complete.ticket");
        pumpUntil([] { return !g_jobRunning; });
        checkIdle(L"Job status: Complete");
        clickStop();
        checkIdle(L"Job status: Complete");
        DestroyWindow(g_mainWindow);
        std::filesystem::remove_all(testDirectory);
        std::cout << "Windows GUI Stop checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        // Do not leave the fixture core alive if an assertion fails.
        if (g_jobRunning && g_activeJob) {
            stopJob();
            try { pumpUntil([] { return !g_jobRunning; }); } catch (...) {}
        }
        std::cerr << error.what() << "\nTest artifacts: " << testDirectory << '\n';
        return 1;
    }
}
