#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#ifdef _MSC_VER
#pragma comment(linker,                                                                                 \
                "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' "       \
                "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' "      \
                "language='*'\"")
#endif

namespace {
constexpr wchar_t kMainWindowClass[] = L"StockUniverseBuilderMainWindow";
constexpr wchar_t kCredentialWindowClass[] = L"StockUniverseBuilderCredentialWindow";
constexpr wchar_t kCoreExecutable[] = L"UniverseBuilderCore.exe";
constexpr wchar_t kApiKeysFile[] = L"APIKeys.txt";
constexpr wchar_t kTicketFile[] = L"ticket.txt";
constexpr wchar_t kRefreshTicketFile[] = L"refresh_stock_list.ticket.txt";
constexpr wchar_t kUniverseFile[] = L"manifests\\asset_universe.csv";

constexpr UINT kMessageLogLine = WM_APP + 1;
constexpr UINT kMessageProgress = WM_APP + 2;
constexpr UINT kMessageJobFinished = WM_APP + 3;

enum ControlId {
    IdCredentialButton = 100,
    IdTimeframe,
    IdStartDate,
    IdEndDate,
    IdFeed,
    IdStockCountMode,
    IdCustomCount,
    IdReuseExisting,
    IdRefreshExisting,
    IdRefreshButton,
    IdPullButton,
    IdOpenDataButton,
    IdLog,
    IdProgress,
    IdCredentialKey = 200,
    IdCredentialSecret
};

enum class JobKind { RefreshStockList, PullData };

struct ProgressUpdate {
    int current = 0;
    int total = 0;
    std::wstring activity;
};

struct JobResult {
    JobKind kind = JobKind::PullData;
    DWORD exitCode = 1;
    bool continueWithPull = false;
    std::wstring launchError;
};

struct CredentialDialogState {
    HWND owner = nullptr;
    HWND keyEdit = nullptr;
    HWND secretEdit = nullptr;
    bool saved = false;
};

HINSTANCE g_instance = nullptr;
HWND g_mainWindow = nullptr;
HWND g_credentialStatus = nullptr;
HWND g_credentialButton = nullptr;
HWND g_timeframe = nullptr;
HWND g_startDate = nullptr;
HWND g_endDate = nullptr;
HWND g_feed = nullptr;
HWND g_stockCountMode = nullptr;
HWND g_customCountLabel = nullptr;
HWND g_customCount = nullptr;
HWND g_reuseExisting = nullptr;
HWND g_refreshExisting = nullptr;
HWND g_refreshButton = nullptr;
HWND g_pullButton = nullptr;
HWND g_openDataButton = nullptr;
HWND g_jobStatus = nullptr;
HWND g_activity = nullptr;
HWND g_progress = nullptr;
HWND g_log = nullptr;
HFONT g_font = nullptr;
HFONT g_titleFont = nullptr;
std::filesystem::path g_applicationDirectory;
bool g_jobRunning = false;

std::filesystem::path applicationPath(const wchar_t* relativePath) {
    return g_applicationDirectory / relativePath;
}

bool fileExists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

std::wstring windowsErrorMessage(DWORD errorCode) {
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, errorCode, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wstring result = length != 0 && message != nullptr ? std::wstring(message, length)
                                                            : L"Windows error " +
                                                                  std::to_wstring(errorCode);
    if (message != nullptr) LocalFree(message);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) {
        result.pop_back();
    }
    return result;
}

std::string toUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                            static_cast<int>(value.size()), nullptr, 0, nullptr,
                                            nullptr);
    if (needed <= 0) return {};
    std::string result(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(),
                        needed, nullptr, nullptr);
    return result;
}

std::wstring fromBytes(const std::string& value) {
    if (value.empty()) return {};
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int needed = MultiByteToWideChar(codePage, flags, value.data(),
                                     static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0) {
        codePage = CP_ACP;
        flags = 0;
        needed = MultiByteToWideChar(codePage, flags, value.data(),
                                     static_cast<int>(value.size()), nullptr, 0);
    }
    if (needed <= 0) return L"[The core produced text that could not be displayed.]";
    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()),
                        result.data(), needed);
    return result;
}

std::wstring controlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1);
    GetWindowTextW(control, buffer.data(), static_cast<int>(buffer.size()));
    return std::wstring(buffer.data());
}

void setControlFont(HWND control, HFONT font = nullptr) {
    SendMessageW(control, WM_SETFONT,
                 reinterpret_cast<WPARAM>(font != nullptr ? font : g_font), TRUE);
}

HWND createControl(DWORD extendedStyle, const wchar_t* className, const wchar_t* text,
                   DWORD style, int x, int y, int width, int height, HWND parent, int id) {
    HWND control = CreateWindowExW(extendedStyle, className, text, WS_CHILD | style, x, y, width,
                                   height, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   g_instance, nullptr);
    if (control != nullptr) setControlFont(control);
    return control;
}

void appendLog(const std::wstring& text) {
    if (g_log == nullptr) return;
    const LRESULT length = SendMessageW(g_log, WM_GETTEXTLENGTH, 0, 0);
    SendMessageW(g_log, EM_SETSEL, static_cast<WPARAM>(length), static_cast<LPARAM>(length));
    SendMessageW(g_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(g_log, EM_SCROLLCARET, 0, 0);
}

bool writeUtf8File(const std::filesystem::path& path, const std::string& contents,
                   std::wstring& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        error = L"Could not open " + path.filename().wstring() +
                L" for writing. Check that the extracted folder is writable.";
        return false;
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.close();
    if (!output) {
        error = L"Could not finish writing " + path.filename().wstring() + L".";
        return false;
    }
    return true;
}

void updateCredentialStatus() {
    SetWindowTextW(g_credentialStatus,
                   fileExists(applicationPath(kApiKeysFile))
                       ? L"Alpaca credentials: Configured"
                       : L"Alpaca credentials: Not configured");
}

void setJobControlsEnabled(bool enabled) {
    EnableWindow(g_pullButton, enabled);
    EnableWindow(g_refreshButton, enabled);
    EnableWindow(g_credentialButton, enabled);
}

void updateCustomCountVisibility() {
    const int selection = static_cast<int>(SendMessageW(g_stockCountMode, CB_GETCURSEL, 0, 0));
    const int show = selection == 1 ? SW_SHOW : SW_HIDE;
    ShowWindow(g_customCountLabel, show);
    ShowWindow(g_customCount, show);
}

bool getSelectedDate(HWND dateControl, SYSTEMTIME& date) {
    return SendMessageW(dateControl, DTM_GETSYSTEMTIME, 0,
                        reinterpret_cast<LPARAM>(&date)) == GDT_VALID;
}

std::string isoDate(const SYSTEMTIME& date, bool endOfDay) {
    char buffer[32]{};
    const int written = snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%sZ",
                                 static_cast<unsigned>(date.wYear),
                                 static_cast<unsigned>(date.wMonth),
                                 static_cast<unsigned>(date.wDay),
                                 endOfDay ? "23:59:59" : "00:00:00");
    return written > 0 ? std::string(buffer, static_cast<std::size_t>(written)) : std::string();
}

std::string comboSelection(HWND combo) {
    const LRESULT selection = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR) return {};
    const LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN,
                                        static_cast<WPARAM>(selection), 0);
    if (length == CB_ERR) return {};
    std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1);
    SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(selection),
                 reinterpret_cast<LPARAM>(buffer.data()));
    return toUtf8(buffer.data());
}

bool buildHistoricalTicket(std::wstring& error) {
    SYSTEMTIME start{};
    SYSTEMTIME end{};
    if (!getSelectedDate(g_startDate, start) || !getSelectedDate(g_endDate, end)) {
        error = L"Choose both a start date and an end date.";
        return false;
    }
    const auto startValue = std::make_tuple(start.wYear, start.wMonth, start.wDay);
    const auto endValue = std::make_tuple(end.wYear, end.wMonth, end.wDay);
    if (startValue > endValue) {
        error = L"The start date cannot be after the end date.";
        return false;
    }

    std::uint64_t maxSymbols = 10;
    const int countMode =
        static_cast<int>(SendMessageW(g_stockCountMode, CB_GETCURSEL, 0, 0));
    if (countMode == 1) {
        const std::wstring customText = controlText(g_customCount);
        if (customText.empty() ||
            !std::all_of(customText.begin(), customText.end(),
                         [](wchar_t character) { return character >= L'0' && character <= L'9'; })) {
            error = L"Enter a whole number greater than zero for the custom stock count.";
            return false;
        }
        try {
            maxSymbols = std::stoull(customText);
        } catch (...) {
            error = L"The custom stock count is too large.";
            return false;
        }
        if (maxSymbols == 0) {
            error = L"Custom count must be greater than zero. Choose Full universe for all stocks.";
            return false;
        }
    } else if (countMode == 2) {
        maxSymbols = 0;
    }

    const std::string timeframe = comboSelection(g_timeframe);
    const std::string feed = comboSelection(g_feed);
    if (timeframe.empty() || feed.empty()) {
        error = L"Choose a timeframe and a data feed.";
        return false;
    }

    const bool reuse = SendMessageW(g_reuseExisting, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool refresh = SendMessageW(g_refreshExisting, BM_GETCHECK, 0, 0) == BST_CHECKED;
    std::ostringstream ticket;
    ticket << "mode=build_universe\n"
           << "api_keys=APIKeys.txt\n"
           << "universe=manifests/asset_universe.csv\n"
           << "data_directory=Data\n"
           << "manifest_output=manifests/build_manifest.csv\n"
           << "timeframe=" << timeframe << '\n'
           << "start=" << isoDate(start, false) << '\n'
           << "end=" << isoDate(end, true) << '\n'
           << "feed=" << feed << '\n'
           << "limit=10000\n"
           << "max_symbols=" << maxSymbols << '\n'
           << "reuse_existing=" << (reuse ? "true" : "false") << '\n'
           << "refresh_existing=" << (refresh ? "true" : "false") << '\n';
    return writeUtf8File(applicationPath(kTicketFile), ticket.str(), error);
}

bool writeRefreshTicket(std::wstring& error) {
    return writeUtf8File(applicationPath(kRefreshTicketFile),
                         "mode=refresh_asset_universe\n"
                         "api_keys=APIKeys.txt\n"
                         "universe=manifests/asset_universe.csv\n",
                         error);
}

void postLogLine(HWND window, const std::string& line) {
    std::wstring text = fromBytes(line);
    if (!text.empty() && text.back() == L'\r') text.pop_back();
    text += L"\r\n";
    auto* allocated = new std::wstring(std::move(text));
    if (!PostMessageW(window, kMessageLogLine, 0, reinterpret_cast<LPARAM>(allocated))) {
        delete allocated;
    }
}

void postProgress(HWND window, const std::string& line) {
    if (line.size() < 5 || line.front() != '[') return;
    const std::size_t slash = line.find('/', 1);
    const std::size_t close = slash == std::string::npos ? std::string::npos
                                                         : line.find(']', slash + 1);
    if (slash == std::string::npos || close == std::string::npos) return;
    const std::string currentText = line.substr(1, slash - 1);
    const std::string totalText = line.substr(slash + 1, close - slash - 1);
    const auto digitsOnly = [](const std::string& value) {
        return !value.empty() &&
               std::all_of(value.begin(), value.end(),
                           [](char character) { return character >= '0' && character <= '9'; });
    };
    if (!digitsOnly(currentText) || !digitsOnly(totalText)) return;
    try {
        const unsigned long long current = std::stoull(currentText);
        const unsigned long long total = std::stoull(totalText);
        if (total == 0 || current > static_cast<unsigned long long>(INT_MAX) ||
            total > static_cast<unsigned long long>(INT_MAX)) {
            return;
        }
        auto* update = new ProgressUpdate;
        update->current = static_cast<int>(current);
        update->total = static_cast<int>(total);
        update->activity = fromBytes(line);
        if (!PostMessageW(window, kMessageProgress, 0, reinterpret_cast<LPARAM>(update))) {
            delete update;
        }
    } catch (...) {
    }
}

void runCoreProcess(HWND window, const std::filesystem::path& corePath,
                    const std::filesystem::path& ticketPath, JobKind kind,
                    bool continueWithPull) {
    auto result = std::make_unique<JobResult>();
    result->kind = kind;
    result->continueWithPull = continueWithPull;

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
        result->launchError = L"Could not create the output pipe: " +
                              windowsErrorMessage(GetLastError());
        PostMessageW(window, kMessageJobFinished, 0,
                     reinterpret_cast<LPARAM>(result.release()));
        return;
    }
    if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
        const DWORD pipeError = GetLastError();
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        result->launchError = L"Could not prepare the output pipe: " +
                              windowsErrorMessage(pipeError);
        PostMessageW(window, kMessageJobFinished, 0,
                     reinterpret_cast<LPARAM>(result.release()));
        return;
    }

    HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = nullInput != INVALID_HANDLE_VALUE ? nullInput : GetStdHandle(STD_INPUT_HANDLE);

    // The child runs in the release directory, so keep its ticket argument relative.
    // This also avoids passing a potentially non-ASCII absolute path through main(char**).
    std::wstring commandLine = L"\"" + corePath.wstring() + L"\" \"" +
                               ticketPath.filename().wstring() + L"\"";
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(
        corePath.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
        g_applicationDirectory.c_str(), &startup, &process);
    const DWORD createError = created ? ERROR_SUCCESS : GetLastError();
    CloseHandle(writePipe);
    if (nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);

    if (!created) {
        CloseHandle(readPipe);
        result->launchError = L"Could not start UniverseBuilderCore.exe: " +
                              windowsErrorMessage(createError);
        PostMessageW(window, kMessageJobFinished, 0,
                     reinterpret_cast<LPARAM>(result.release()));
        return;
    }

    CloseHandle(process.hThread);
    std::string pending;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer, static_cast<DWORD>(sizeof(buffer)), &bytesRead, nullptr) &&
           bytesRead != 0) {
        pending.append(buffer, buffer + bytesRead);
        std::size_t newline = std::string::npos;
        while ((newline = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            postProgress(window, line);
            postLogLine(window, line);
        }
    }
    if (!pending.empty()) {
        postProgress(window, pending);
        postLogLine(window, pending);
    }
    CloseHandle(readPipe);

    WaitForSingleObject(process.hProcess, INFINITE);
    if (!GetExitCodeProcess(process.hProcess, &result->exitCode)) {
        result->exitCode = 1;
        result->launchError = L"The core stopped, but its exit status could not be read: " +
                              windowsErrorMessage(GetLastError());
    }
    CloseHandle(process.hProcess);
    PostMessageW(window, kMessageJobFinished, 0, reinterpret_cast<LPARAM>(result.release()));
}

void launchJob(JobKind kind, const std::filesystem::path& ticketPath,
               bool continueWithPull, bool clearLog) {
    if (g_jobRunning) return;
    const std::filesystem::path corePath = applicationPath(kCoreExecutable);
    if (!fileExists(corePath)) {
        MessageBoxW(g_mainWindow,
                    L"UniverseBuilderCore.exe is missing. Keep both application files in the "
                    L"extracted release folder.",
                    L"Stock Universe Builder", MB_OK | MB_ICONERROR);
        return;
    }
    if (clearLog) SetWindowTextW(g_log, L"");
    appendLog(kind == JobKind::RefreshStockList
                  ? L"Starting stock-list refresh...\r\n"
                  : L"Starting historical-data pull...\r\n");
    SetWindowTextW(g_jobStatus, L"Job status: Running");
    SetWindowTextW(g_activity, kind == JobKind::RefreshStockList
                                   ? L"Current activity: Refreshing the stock list"
                                   : L"Current activity: Starting the core");
    SendMessageW(g_progress, PBM_SETPOS, 0, 0);
    SendMessageW(g_progress, PBM_SETMARQUEE, kind == JobKind::RefreshStockList ? TRUE : FALSE, 35);
    g_jobRunning = true;
    setJobControlsEnabled(false);
    try {
        std::thread(runCoreProcess, g_mainWindow, corePath, ticketPath, kind,
                    continueWithPull).detach();
    } catch (const std::exception&) {
        g_jobRunning = false;
        setJobControlsEnabled(true);
        SendMessageW(g_progress, PBM_SETMARQUEE, FALSE, 0);
        SetWindowTextW(g_jobStatus, L"Job status: Failed");
        MessageBoxW(g_mainWindow, L"Could not create the background worker thread.",
                    L"Stock Universe Builder", MB_OK | MB_ICONERROR);
    }
}

bool credentialsConfigured() {
    if (fileExists(applicationPath(kApiKeysFile))) return true;
    MessageBoxW(g_mainWindow,
                L"Alpaca credentials are not configured. Click Set Alpaca Credentials first.",
                L"Stock Universe Builder", MB_OK | MB_ICONINFORMATION);
    return false;
}

void beginRefresh(bool continueWithPull, bool clearLog) {
    if (!credentialsConfigured()) return;
    std::wstring error;
    if (!writeRefreshTicket(error)) {
        MessageBoxW(g_mainWindow, error.c_str(), L"Stock Universe Builder",
                    MB_OK | MB_ICONERROR);
        return;
    }
    launchJob(JobKind::RefreshStockList, applicationPath(kRefreshTicketFile),
              continueWithPull, clearLog);
}

void beginPull() {
    if (!credentialsConfigured()) return;
    std::wstring error;
    if (!buildHistoricalTicket(error)) {
        MessageBoxW(g_mainWindow, error.c_str(), L"Stock Universe Builder",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    if (!fileExists(applicationPath(kUniverseFile))) {
        const int answer = MessageBoxW(
            g_mainWindow,
            L"The stock list has not been downloaded yet. Download it now?",
            L"Stock Universe Builder", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1);
        if (answer != IDYES) return;
        beginRefresh(true, true);
        return;
    }
    launchJob(JobKind::PullData, applicationPath(kTicketFile), false, true);
}

LRESULT CALLBACK credentialWindowProc(HWND window, UINT message, WPARAM wParam,
                                      LPARAM lParam) {
    auto* state = reinterpret_cast<CredentialDialogState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
        case WM_CREATE: {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = static_cast<CredentialDialogState*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            createControl(0, L"STATIC", L"API Key ID:", WS_VISIBLE, 20, 22, 100, 22, window, 0);
            state->keyEdit = createControl(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                           WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 125, 18,
                                           270, 26, window, IdCredentialKey);
            createControl(0, L"STATIC", L"Secret Key:", WS_VISIBLE, 20, 66, 100, 22, window, 0);
            state->secretEdit = createControl(
                WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, 125, 62, 270, 26,
                window, IdCredentialSecret);
            SendMessageW(state->secretEdit, EM_SETPASSWORDCHAR, 0x25CF, 0);
            createControl(0, L"BUTTON", L"Save",
                          WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 225, 112, 80, 28,
                          window, IDOK);
            createControl(0, L"BUTTON", L"Cancel", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                          315, 112, 80, 28, window, IDCANCEL);
            SetFocus(state->keyEdit);
            return 0;
        }
        case WM_COMMAND:
            if (state == nullptr) break;
            if (LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(window);
                return 0;
            }
            if (LOWORD(wParam) == IDOK) {
                std::wstring key = controlText(state->keyEdit);
                std::wstring secret = controlText(state->secretEdit);
                if (key.empty() || secret.empty()) {
                    MessageBoxW(window, L"Enter both the API key ID and secret key.",
                                L"Alpaca Credentials", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                if (key.find_first_of(L"\r\n") != std::wstring::npos ||
                    secret.find_first_of(L"\r\n") != std::wstring::npos) {
                    MessageBoxW(window, L"Credentials cannot contain a line break.",
                                L"Alpaca Credentials", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                std::wstring error;
                const std::string contents = "https://paper-api.alpaca.markets\n" +
                                             toUtf8(key) + "\n" + toUtf8(secret) + "\n";
                if (!writeUtf8File(applicationPath(kApiKeysFile), contents, error)) {
                    MessageBoxW(window, error.c_str(), L"Alpaca Credentials",
                                MB_OK | MB_ICONERROR);
                    return 0;
                }
                std::fill(secret.begin(), secret.end(), L'\0');
                state->saved = true;
                DestroyWindow(window);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void showCredentialDialog() {
    CredentialDialogState state;
    state.owner = g_mainWindow;
    RECT ownerRect{};
    GetWindowRect(g_mainWindow, &ownerRect);
    constexpr int width = 430;
    constexpr int height = 195;
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, kCredentialWindowClass,
                                  L"Set Alpaca Credentials", WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                  x, y, width, height, g_mainWindow, nullptr, g_instance, &state);
    if (dialog == nullptr) {
        MessageBoxW(g_mainWindow, L"Could not open the credential window.",
                    L"Stock Universe Builder", MB_OK | MB_ICONERROR);
        return;
    }
    EnableWindow(g_mainWindow, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    MSG message{};
    while (IsWindow(dialog)) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(g_mainWindow, TRUE);
    SetForegroundWindow(g_mainWindow);
    if (state.saved) {
        updateCredentialStatus();
        appendLog(L"Alpaca credentials saved to APIKeys.txt.\r\n");
    }
}

void openDataFolder() {
    const std::filesystem::path dataPath = applicationPath(L"Data");
    std::error_code error;
    std::filesystem::create_directories(dataPath, error);
    if (error) {
        const std::wstring message = L"Could not create the Data folder: " +
                                     fromBytes(error.message());
        MessageBoxW(g_mainWindow, message.c_str(), L"Stock Universe Builder",
                    MB_OK | MB_ICONERROR);
        return;
    }
    const HINSTANCE result = ShellExecuteW(g_mainWindow, L"open", dataPath.c_str(), nullptr,
                                           g_applicationDirectory.c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(g_mainWindow, L"Windows could not open the Data folder.",
                    L"Stock Universe Builder", MB_OK | MB_ICONERROR);
    }
}

void addComboItem(HWND combo, const wchar_t* value) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
}

void createMainControls(HWND window) {
    HWND title = createControl(0, L"STATIC", L"Stock Universe Builder", WS_VISIBLE, 24, 18,
                               400, 34, window, 0);
    setControlFont(title, g_titleFont);
    createControl(0, L"STATIC", L"ALPACA", WS_VISIBLE, 28, 62, 72, 24, window, 0);
    g_credentialStatus = createControl(0, L"STATIC", L"", WS_VISIBLE, 105, 62, 320, 24,
                                       window, 0);
    g_credentialButton = createControl(0, L"BUTTON", L"Set Alpaca Credentials",
                                       WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 28, 88, 210, 30,
                                       window, IdCredentialButton);

    createControl(0, L"BUTTON", L"DATA SETTINGS", WS_VISIBLE | BS_GROUPBOX, 20, 130, 824, 296,
                  window, 0);
    createControl(0, L"STATIC", L"Timeframe:", WS_VISIBLE, 46, 166, 125, 22, window, 0);
    g_timeframe = createControl(0, WC_COMBOBOXW, L"",
                                WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                190, 162, 170, 220, window, IdTimeframe);
    for (const wchar_t* value : {L"1Min", L"5Min", L"15Min", L"30Min", L"1Hour", L"1Day"}) {
        addComboItem(g_timeframe, value);
    }
    SendMessageW(g_timeframe, CB_SETCURSEL, 5, 0);

    createControl(0, L"STATIC", L"Start Date:", WS_VISIBLE, 46, 204, 125, 22, window, 0);
    g_startDate = createControl(0, DATETIMEPICK_CLASSW, L"",
                                WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATEFORMAT, 190, 200, 170, 26,
                                window, IdStartDate);
    createControl(0, L"STATIC", L"End Date:", WS_VISIBLE, 46, 242, 125, 22, window, 0);
    g_endDate = createControl(0, DATETIMEPICK_CLASSW, L"",
                              WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATEFORMAT, 190, 238, 170, 26,
                              window, IdEndDate);
    DateTime_SetFormat(g_startDate, L"yyyy-MM-dd");
    DateTime_SetFormat(g_endDate, L"yyyy-MM-dd");
    SYSTEMTIME end{};
    GetLocalTime(&end);
    SYSTEMTIME start = end;
    if (start.wYear > 1) --start.wYear;
    if (start.wMonth == 2 && start.wDay == 29) start.wDay = 28;
    DateTime_SetSystemtime(g_startDate, GDT_VALID, &start);
    DateTime_SetSystemtime(g_endDate, GDT_VALID, &end);

    createControl(0, L"STATIC", L"Feed:", WS_VISIBLE, 46, 280, 125, 22, window, 0);
    g_feed = createControl(0, WC_COMBOBOXW, L"",
                           WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                           190, 276, 170, 100, window, IdFeed);
    addComboItem(g_feed, L"iex");
    addComboItem(g_feed, L"sip");
    SendMessageW(g_feed, CB_SETCURSEL, 0, 0);

    createControl(0, L"STATIC", L"Stocks to pull:", WS_VISIBLE, 46, 318, 125, 22, window, 0);
    g_stockCountMode = createControl(
        0, WC_COMBOBOXW, L"", WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        190, 314, 210, 130, window, IdStockCountMode);
    addComboItem(g_stockCountMode, L"Small test (10)");
    addComboItem(g_stockCountMode, L"Custom count");
    addComboItem(g_stockCountMode, L"Full universe");
    SendMessageW(g_stockCountMode, CB_SETCURSEL, 0, 0);
    g_customCountLabel = createControl(0, L"STATIC", L"Count:", 0, 430, 318, 60, 22, window, 0);
    g_customCount = createControl(WS_EX_CLIENTEDGE, L"EDIT", L"100",
                                  WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, 490, 314, 100, 26,
                                  window, IdCustomCount);
    SendMessageW(g_customCount, EM_SETLIMITTEXT, 9, 0);
    updateCustomCountVisibility();

    g_reuseExisting = createControl(0, L"BUTTON", L"Reuse existing data",
                                    WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 46, 360, 240, 24,
                                    window, IdReuseExisting);
    SendMessageW(g_reuseExisting, BM_SETCHECK, BST_CHECKED, 0);
    g_refreshExisting = createControl(0, L"BUTTON", L"Refresh existing data",
                                      WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 46, 392, 240, 24,
                                      window, IdRefreshExisting);
    SendMessageW(g_refreshExisting, BM_SETCHECK, BST_UNCHECKED, 0);

    g_refreshButton = createControl(0, L"BUTTON", L"Refresh Stock List",
                                    WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 24, 446, 190, 34,
                                    window, IdRefreshButton);
    g_pullButton = createControl(0, L"BUTTON", L"Pull Data",
                                 WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 226, 446, 190, 34,
                                 window, IdPullButton);
    g_openDataButton = createControl(0, L"BUTTON", L"Open Data Folder",
                                     WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 428, 446, 190, 34,
                                     window, IdOpenDataButton);

    g_jobStatus = createControl(0, L"STATIC", L"Job status: Ready", WS_VISIBLE, 24, 496, 300,
                                22, window, 0);
    g_progress = createControl(0, PROGRESS_CLASSW, L"", WS_VISIBLE | PBS_SMOOTH | PBS_MARQUEE,
                               24, 522, 820, 20, window, IdProgress);
    SendMessageW(g_progress, PBM_SETRANGE32, 0, 100);
    g_activity = createControl(0, L"STATIC", L"Current activity: Waiting", WS_VISIBLE, 24, 550,
                               820, 22, window, 0);
    createControl(0, L"STATIC", L"Core output:", WS_VISIBLE, 24, 578, 120, 20, window, 0);
    g_log = createControl(WS_EX_CLIENTEDGE, L"EDIT", L"",
                          WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL |
                              ES_READONLY | WS_VSCROLL,
                          24, 602, 820, 92, window, IdLog);
    SendMessageW(g_log, EM_SETLIMITTEXT, 16 * 1024 * 1024, 0);
    updateCredentialStatus();
}

LRESULT CALLBACK mainWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            createMainControls(window);
            return 0;
        case WM_COMMAND:
            if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IdStockCountMode) {
                updateCustomCountVisibility();
                return 0;
            }
            if (HIWORD(wParam) == BN_CLICKED) {
                switch (LOWORD(wParam)) {
                    case IdCredentialButton:
                        showCredentialDialog();
                        return 0;
                    case IdRefreshButton:
                        beginRefresh(false, true);
                        return 0;
                    case IdPullButton:
                        beginPull();
                        return 0;
                    case IdOpenDataButton:
                        openDataFolder();
                        return 0;
                    default:
                        break;
                }
            }
            break;
        case WM_CLOSE:
            if (g_jobRunning) {
                MessageBoxW(window, L"A job is still running. Wait for it to finish before closing.",
                            L"Stock Universe Builder", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case kMessageLogLine: {
            std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
            appendLog(*text);
            return 0;
        }
        case kMessageProgress: {
            std::unique_ptr<ProgressUpdate> update(reinterpret_cast<ProgressUpdate*>(lParam));
            SendMessageW(g_progress, PBM_SETMARQUEE, FALSE, 0);
            SendMessageW(g_progress, PBM_SETRANGE32, 0, update->total);
            SendMessageW(g_progress, PBM_SETPOS, update->current, 0);
            const std::wstring activity = L"Current activity: " + update->activity;
            SetWindowTextW(g_activity, activity.c_str());
            return 0;
        }
        case kMessageJobFinished: {
            std::unique_ptr<JobResult> result(reinterpret_cast<JobResult*>(lParam));
            SendMessageW(g_progress, PBM_SETMARQUEE, FALSE, 0);
            g_jobRunning = false;
            setJobControlsEnabled(true);
            if (result->kind == JobKind::RefreshStockList) {
                std::error_code ignored;
                std::filesystem::remove(applicationPath(kRefreshTicketFile), ignored);
            }
            if (!result->launchError.empty() || result->exitCode != 0) {
                SetWindowTextW(g_jobStatus, L"Job status: Failed");
                SetWindowTextW(g_activity, L"Current activity: The core reported an error");
                if (!result->launchError.empty()) {
                    appendLog(L"[GUI] " + result->launchError + L"\r\n");
                }
                std::wstring jobMessage = result->launchError.empty()
                                              ? L"The job failed. Read the core output for details."
                                              : result->launchError;
                MessageBoxW(window, jobMessage.c_str(), L"Stock Universe Builder",
                            MB_OK | MB_ICONERROR);
                return 0;
            }

            if (result->kind == JobKind::RefreshStockList && result->continueWithPull) {
                appendLog(L"\r\nStock list refreshed successfully. Continuing with the data "
                          L"pull.\r\n\r\n");
                launchJob(JobKind::PullData, applicationPath(kTicketFile), false, false);
                return 0;
            }
            if (result->kind == JobKind::RefreshStockList) {
                SetWindowTextW(g_jobStatus, L"Job status: Complete");
                SetWindowTextW(g_activity, L"Current activity: Stock list refreshed successfully");
                appendLog(L"Stock-list refresh complete.\r\n");
            } else {
                SetWindowTextW(g_jobStatus, L"Job status: Complete");
                SetWindowTextW(g_activity, L"Current activity: Historical-data pull complete");
                appendLog(L"Historical-data pull complete.\r\n");
            }
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool registerWindowClasses() {
    WNDCLASSEXW mainClass{};
    mainClass.cbSize = sizeof(mainClass);
    mainClass.style = CS_HREDRAW | CS_VREDRAW;
    mainClass.lpfnWndProc = mainWindowProc;
    mainClass.hInstance = g_instance;
    mainClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    mainClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    mainClass.lpszClassName = kMainWindowClass;
    mainClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    if (RegisterClassExW(&mainClass) == 0) return false;

    WNDCLASSEXW credentialClass = mainClass;
    credentialClass.lpfnWndProc = credentialWindowProc;
    credentialClass.lpszClassName = kCredentialWindowClass;
    return RegisterClassExW(&credentialClass) != 0;
}

std::filesystem::path executableDirectory() {
    std::vector<wchar_t> buffer(1024);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size() - 1) {
            return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    g_instance = instance;
    g_applicationDirectory = executableDirectory();
    if (g_applicationDirectory.empty() ||
        !SetCurrentDirectoryW(g_applicationDirectory.c_str())) {
        MessageBoxW(nullptr, L"Could not use the application folder.",
                    L"Stock Universe Builder", MB_OK | MB_ICONERROR);
        return 1;
    }

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_DATE_CLASSES | ICC_PROGRESS_CLASS;
    if (!InitCommonControlsEx(&controls)) {
        MessageBoxW(nullptr, L"Could not initialize Windows controls.",
                    L"Stock Universe Builder", MB_OK | MB_ICONERROR);
        return 1;
    }
    SetProcessDPIAware();
    g_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_titleFont = CreateFontW(-28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!registerWindowClasses()) {
        MessageBoxW(nullptr, L"Could not initialize the application window.",
                    L"Stock Universe Builder", MB_OK | MB_ICONERROR);
        return 1;
    }

    std::error_code ignored;
    std::filesystem::create_directories(applicationPath(L"Data"), ignored);
    std::filesystem::create_directories(applicationPath(L"manifests"), ignored);
    g_mainWindow = CreateWindowExW(
        0, kMainWindowClass, L"Stock Universe Builder",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
        880, 740, nullptr, nullptr, instance, nullptr);
    if (g_mainWindow == nullptr) {
        MessageBoxW(nullptr, L"Could not create the application window.",
                    L"Stock Universe Builder", MB_OK | MB_ICONERROR);
        return 1;
    }
    ShowWindow(g_mainWindow, showCommand);
    UpdateWindow(g_mainWindow);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (g_font != nullptr) DeleteObject(g_font);
    if (g_titleFont != nullptr) DeleteObject(g_titleFont);
    return static_cast<int>(message.wParam);
}
