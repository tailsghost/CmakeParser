#pragma once

#include <windows.h>
#include <string>
#include <mutex>
#include <atomic>
#include <vector>
#include <thread>
#include <condition_variable>
#include <sstream>

struct ProcessRunGuardResult {
    std::wstring stdoutText;
    std::wstring stderrText;
    int code{ -1 };
    bool success{ false };
};

class ProcessRunGuard {
public:
    ProcessRunGuard()
        : processHandle(nullptr),
        stdinWrite(nullptr),
        stdoutRead(nullptr),
        stderrRead(nullptr),
        stopThread(false),
        currentCmdRunning(false)
    {
    }

    ~ProcessRunGuard() {
        stopThread = true;

        // Close stdin to let PowerShell exit on its own if possible
        {
            std::lock_guard<std::mutex> lk(handleMutex);
            if (stdinWrite) { CloseHandle(stdinWrite); stdinWrite = nullptr; }
        }

        if (readerThread.joinable()) readerThread.join();

        CloseHandles();
    }

    // «апускаем PowerShell один раз. ¬озвращает true при успешном старте.
    bool StartProcess(const std::wstring& workDir = L"") {
        std::lock_guard<std::mutex> lk(handleMutex);

        if (processHandle) return true; // уже запущено

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        // pipe: parent reads child's stdout
        HANDLE outReadParent = nullptr, outWriteChild = nullptr;
        // pipe: parent reads child's stderr
        HANDLE errReadParent = nullptr, errWriteChild = nullptr;
        // pipe: parent writes to child's stdin
        HANDLE inReadChild = nullptr, inWriteParent = nullptr;

        if (!CreatePipe(&outReadParent, &outWriteChild, &sa, 0)) {
            Close(outReadParent, outWriteChild, errReadParent, errWriteChild, inReadChild, inWriteParent);
            return false;
        }
        if (!CreatePipe(&errReadParent, &errWriteChild, &sa, 0)) {
            Close(outReadParent, outWriteChild, errReadParent, errWriteChild, inReadChild, inWriteParent);
            return false;
        }
        if (!CreatePipe(&inReadChild, &inWriteParent, &sa, 0)) {
            Close(outReadParent, outWriteChild, errReadParent, errWriteChild, inReadChild, inWriteParent);
            return false;
        };

        // Make sure the parent handles are NOT inheritable
        SetHandleInformation(outReadParent, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(errReadParent, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(inWriteParent, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = inReadChild;      // child's stdin
        si.hStdOutput = outWriteChild;   // child's stdout
        si.hStdError = errWriteChild;    // child's stderr

        PROCESS_INFORMATION pi{};
        // We'll pass a modifiable buffer to CreateProcessW
        std::wstring cmdLine = L"powershell.exe -NoLogo -NoExit -Command -";
        std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
        cmdBuf.push_back(0);

        BOOL ok = CreateProcessW(
            nullptr,
            cmdBuf.data(),
            nullptr, nullptr,
            TRUE,                    // inherit handles (so child gets the pipe ends)
            CREATE_NO_WINDOW,
            nullptr,
            workDir.empty() ? nullptr : const_cast<LPWSTR>(workDir.c_str()),
            &si,
            &pi
        );

        // Parent doesn't need child's ends
        CloseHandle(outWriteChild); outWriteChild = nullptr;
        CloseHandle(errWriteChild); errWriteChild = nullptr;
        CloseHandle(inReadChild); inReadChild = nullptr;

        if (!ok) {
            DWORD err = GetLastError();
            wchar_t* msg = nullptr;
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL, err, 0, (LPWSTR)&msg, 0, NULL);

            OutputDebugStringW(L"CreateProcessW FAILED: ");
            OutputDebugStringW(msg);
            LocalFree(msg);
            // cleanup
            if (outReadParent) { CloseHandle(outReadParent); outReadParent = nullptr; }
            if (errReadParent) { CloseHandle(errReadParent); errReadParent = nullptr; }
            if (inWriteParent) { CloseHandle(inWriteParent); inWriteParent = nullptr; }
            return false;
        }

        // Save handles
        processHandle = pi.hProcess;
        // we don't need thread handle
        CloseHandle(pi.hThread);

        stdinWrite = inWriteParent;
        stdoutRead = outReadParent;
        stderrRead = errReadParent;

        stopThread = false;
        readerThread = std::thread([this]() { this->ReaderLoop(); });

        return true;
    }

    void Close(HANDLE& outReadParent, HANDLE& outWriteChild, HANDLE& errReadParent, HANDLE& errWriteChild, HANDLE& inReadChild, HANDLE& inWriteParent) {
        if (outReadParent) CloseHandle(outReadParent);
        if (outWriteChild) CloseHandle(outWriteChild);
        if (errReadParent) CloseHandle(errReadParent);
        if (errWriteChild) CloseHandle(errWriteChild);
        if (inReadChild) CloseHandle(inReadChild);
        if (inWriteParent) CloseHandle(inWriteParent);
    }

    // ќтправка команды в уже запущенный PowerShell
    ProcessRunGuardResult RunCommand(const std::wstring& cmd) {
        std::unique_lock<std::mutex> lk(cmdMutex);

        if (!processHandle || !stdinWrite) {
            return ProcessRunGuardResult{ L"", L"Process not started", -1, false };
        }

        if (currentCmdRunning) {
            return ProcessRunGuardResult{ L"", L"Previous command still running", -1, false };
        }

        // ‘ормируем команду дл€ PowerShell и маркер конца.
        // $LASTEXITCODE содержит код последней внешней команды; дл€ PowerShell команд тоже можно использовать $LASTEXITCODE.
        // »спользуем Write-Output чтобы гарантированно получить маркер в stdout.
        std::wstring fullCmd = cmd + L"\nWrite-Output \"__END__ $LASTEXITCODE\"\n";

        //  онвертируем в UTF-8 (PowerShell обычно понимает UTF-8 в redirected stdin в современных системах)
        std::string utf8 = WideToUtf8(fullCmd);

        DWORD written = 0;
        {
            std::lock_guard<std::mutex> hl(handleMutex);
            if (!stdinWrite) {
                return ProcessRunGuardResult{ L"", L"stdin closed", -1, false };
            }
            BOOL wok = WriteFile(stdinWrite, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
            if (!wok || written != utf8.size()) {
                return ProcessRunGuardResult{ L"", L"Failed to write to process stdin", -1, false };
            }
            // Ensure data is flushed by flushing the file handle (no direct FlushFileBuffers for anonymous pipes required but harmless)
            FlushFileBuffers(stdinWrite);
        }

        currentCmdRunning = true;
        // wait until reader thread clears currentCmdRunning
        cmdCV.wait(lk, [this]() { return !currentCmdRunning.load(); });

        ProcessRunGuardResult res;
        res.stdoutText = lastStdout;
        res.stderrText = lastStderr;
        res.code = lastExitCode;
        res.success = (lastExitCode == 0);

        // reset stored results for next command
        lastStdout.clear();
        lastStderr.clear();
        lastExitCode = -1;

        return res;
    }

private:
    HANDLE processHandle;
    HANDLE stdinWrite;
    HANDLE stdoutRead;
    HANDLE stderrRead;

    std::thread readerThread;
    std::mutex cmdMutex;
    std::condition_variable cmdCV;
    std::atomic<bool> currentCmdRunning;
    std::atomic<bool> stopThread;

    std::wstring lastStdout;
    std::wstring lastStderr;
    int lastExitCode{ -1 };

    std::mutex handleMutex; // protects handle access/close

    // Helper: UTF conversions
    static std::string WideToUtf8(const std::wstring& w) {
        if (w.empty()) return {};
        int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string out(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &out[0], size, nullptr, nullptr);
        return out;
    }

    static std::wstring Utf8ToWide(const std::string& s) {
        if (s.empty()) return {};
        int size = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring out(size, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], size);
        return out;
    }

    // Non-blocking-ish reader loop: использует PeekNamedPipe чтобы не блокировать чтение одной трубы в ущерб другой.
    void ReaderLoop() {
        std::string outBuffer;
        std::string errBuffer;
        const std::string marker = "__END__";

        std::vector<char> readBuf(8192);

        while (!stopThread) {
            bool didWork = false;

            // stdout
            if (stdoutRead) {
                DWORD avail = 0;
                if (PeekNamedPipe(stdoutRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                    DWORD toRead = (DWORD)std::min<size_t>(readBuf.size(), avail);
                    DWORD actuallyRead = 0;
                    if (ReadFile(stdoutRead, readBuf.data(), toRead, &actuallyRead, nullptr) && actuallyRead > 0) {
                        outBuffer.append(readBuf.data(), readBuf.data() + actuallyRead);
                        didWork = true;

                        // process any markers
                        size_t pos;
                        while ((pos = outBuffer.find(marker)) != std::string::npos) {
                            // find end of line after marker to parse exit code
                            size_t lineStart = (pos >= 0) ? outBuffer.rfind('\n', pos) : std::string::npos;
                            // We consider the marker start as beginning for parsing
                            size_t after = pos + marker.size();
                            // skip spaces
                            while (after < outBuffer.size() && (outBuffer[after] == ' ' || outBuffer[after] == ':')) ++after;
                            // read digits until newline
                            size_t codeStart = after;
                            while (after < outBuffer.size() && (outBuffer[after] == '-' || (outBuffer[after] >= '0' && outBuffer[after] <= '9'))) ++after;
                            std::string codeStr = outBuffer.substr(codeStart, after - codeStart);

                            // stdout content is everything before the marker (trim trailing newlines)
                            std::string stdoutContent = outBuffer.substr(0, pos);
                            // Remove possible trailing \r\n from stdoutContent
                            while (!stdoutContent.empty() && (stdoutContent.back() == '\n' || stdoutContent.back() == '\r'))
                                stdoutContent.pop_back();

                            int code = -1;
                            try {
                                if (!codeStr.empty()) code = std::stoi(codeStr);
                            }
                            catch (...) { code = -1; }

                            {
                                std::lock_guard<std::mutex> lk(cmdMutex);
                                lastStdout = Utf8ToWide(stdoutContent);
                                lastStderr = Utf8ToWide(errBuffer);
                                lastExitCode = code;
                                currentCmdRunning = false;
                            }
                            cmdCV.notify_one();

                            // erase processed part from outBuffer
                            size_t eraseUpTo = after;
                            // also eat the newline after the marker if exists
                            if (eraseUpTo < outBuffer.size() && (outBuffer[eraseUpTo] == '\r' || outBuffer[eraseUpTo] == '\n')) {
                                // skip line endings
                                while (eraseUpTo < outBuffer.size() && (outBuffer[eraseUpTo] == '\r' || outBuffer[eraseUpTo] == '\n')) ++eraseUpTo;
                            }
                            outBuffer.erase(0, eraseUpTo);
                            errBuffer.clear();
                        }
                    }
                }
            }

            // stderr
            if (stderrRead) {
                DWORD avail = 0;
                if (PeekNamedPipe(stderrRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                    DWORD toRead = (DWORD)std::min<size_t>(readBuf.size(), avail);
                    DWORD actuallyRead = 0;
                    if (ReadFile(stderrRead, readBuf.data(), toRead, &actuallyRead, nullptr) && actuallyRead > 0) {
                        errBuffer.append(readBuf.data(), readBuf.data() + actuallyRead);
                        didWork = true;
                    }
                }
            }

            if (!didWork) {
                Sleep(1);
            }
        }
    }

    void CloseHandles() {
        std::lock_guard<std::mutex> lk(handleMutex);
        if (stdinWrite) { CloseHandle(stdinWrite); stdinWrite = nullptr; }
        if (stdoutRead) { CloseHandle(stdoutRead); stdoutRead = nullptr; }
        if (stderrRead) { CloseHandle(stderrRead); stderrRead = nullptr; }
        if (processHandle) {
            // try graceful wait
            DWORD waitRes = WaitForSingleObject(processHandle, 200);
            if (waitRes == WAIT_TIMEOUT) {
                // force terminate
                TerminateProcess(processHandle, 1);
            }
            CloseHandle(processHandle);
            processHandle = nullptr;
        }
    }
};