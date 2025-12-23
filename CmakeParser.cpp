#include "CmakeParser.h"
#include "CommandGenerator.h"
#include "RspFileGenerator.h"
#include "ProcessRunGuard.h"

#include <io.h>
#include <fcntl.h>

using namespace cmakeparser;

static std::string ToUtf8(const std::wstring& w) {
	if (w.empty()) return {};
	int size = WideCharToMultiByte(
		CP_UTF8, 0,
		w.data(), (int)w.size(),
		nullptr, 0, nullptr, nullptr
	);
	std::string s(size, 0);
	WideCharToMultiByte(
		CP_UTF8, 0,
		w.data(), (int)w.size(),
		s.data(), size, nullptr, nullptr
	);
	return s;
}

struct ProcessResult {
	std::wstring stdoutText;
	std::wstring stderrText;
	DWORD exitCode = (DWORD)-1;
};

static std::wstring Utf8ToWide(const std::string& s) {
	if (s.empty()) return {};
	int size = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
	std::wstring out(size, 0);
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], size);
	return out;
}

static ProcessResult RunProcessCreateProcessW(const std::wstring& command, const std::wstring& workDir) {
	ProcessResult result;

	SECURITY_ATTRIBUTES sa{};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE outRead = nullptr, outWrite = nullptr;
	HANDLE errRead = nullptr, errWrite = nullptr;

	CreatePipe(&outRead, &outWrite, &sa, 0);
	CreatePipe(&errRead, &errWrite, &sa, 0);

	SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = outWrite;
	si.hStdError = errWrite;
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

	PROCESS_INFORMATION pi{};

	std::vector<wchar_t> cmdBuf(command.begin(), command.end());
	cmdBuf.push_back(L'\0');

	BOOL ok = CreateProcessW(
		nullptr,
		cmdBuf.data(),   // command line
		nullptr,
		nullptr,
		TRUE,
		0,
		nullptr,
		workDir.c_str(),
		&si,
		&pi
	);

	CloseHandle(outWrite);
	CloseHandle(errWrite);

	if (!ok) {
		result.stderrText = L"CreateProcessW failed";
		return result;
	}

	auto ReadPipe = [](HANDLE h) {
		std::string buf;
		char tmp[4096];
		DWORD read = 0;
		while (ReadFile(h, tmp, sizeof(tmp), &read, nullptr) && read > 0) {
			buf.append(tmp, read);
		}
		return buf;
		};

	std::string outA = ReadPipe(outRead);
	std::string errA = ReadPipe(errRead);

	WaitForSingleObject(pi.hProcess, INFINITE);
	GetExitCodeProcess(pi.hProcess, &result.exitCode);

	CloseHandle(outRead);
	CloseHandle(errRead);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	result.stdoutText = Utf8ToWide(outA);
	result.stderrText = Utf8ToWide(errA);

	return result;
}

int main()
{
	_setmode(_fileno(stdout), _O_U16TEXT);
	_setmode(_fileno(stdin), _O_U16TEXT);
	CmakeParser parser;
	parser.Parse(L"C:/Users/Андрей/source/repos/FBD-editor/FBDEditor/bin/Debug/net10.0-windows/win-x64/NinjaBuilder/M3_CITY2/CMakeLists.txt");
	auto result = parser.GetModel();
	auto commands = parser.GetAST();
	RspFileGenerator rspGenerator(result, parser.GetBuildPath());
	CommandGenerator generator(result, parser.GetBasePath() + L"/NinjaBuilder/tools/gcc-arm-none-eabi-10.3-2021.10/bin/", parser.GetM3Path() + L"/src/mdk-arm/");
	auto objcopy = parser.GetBasePath() + L"/tools/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-objcopy.exe";

	ProcessRunGuard prg;
	if (!prg.StartProcess()) {
		std::wcerr << L"Failed to start process\n";
		return 8;
	}

	size_t count = 0;

	while (generator.HasNext()) {
		std::wstring command;
		auto pathRsp = rspGenerator.CreateNextRspFile();

		generator.Next(command, pathRsp);

		std::wcout << count << L")" << L"\n";
		std::wcout << command << L"\n";

		auto result = RunProcessCreateProcessW(command, parser.GetBuildPath());

		if (result.exitCode != 0) {
			std::wcout << result.stderrText << L"\n";
			return (int)result.exitCode;
		}

		if (!result.stderrText.empty())
			std::wcout << result.stderrText << L"\n";

		count++;
	}

	auto pathElf = parser.GetBuildPath() + L"/MAIN.elf";
	auto pathBin = parser.GetBuildPath() + L"/MAIN.bin";
	auto pathHex = parser.GetBuildPath() + L"/MAIN.hex";
	std::wstring pathLinkFile = rspGenerator.CreateLinkRspFile(generator.GetLinks());
	auto command = generator.CreateLinkCommand(pathLinkFile, pathElf);
	auto commandBin = generator.CreateBinCommand(pathElf, pathBin);
	auto commandHex = generator.CreateHexCommand(pathElf, pathHex);
	{
		auto result = RunProcessCreateProcessW(command, parser.GetBuildPath());

		if (result.exitCode != 0) {
			std::wcout << result.stderrText << L"\n";
			return (int)result.exitCode;
		}
	}

	{
		auto result = RunProcessCreateProcessW(commandBin, parser.GetBuildPath());

		if (result.exitCode != 0) {
			std::wcout << result.stderrText << L"\n";
			return (int)result.exitCode;
		}
	}

	{
		auto result = RunProcessCreateProcessW(commandHex, parser.GetBuildPath());

		if (result.exitCode != 0) {
			std::wcout << result.stderrText << L"\n";
			return (int)result.exitCode;
		}
	}

	return 0;
}
