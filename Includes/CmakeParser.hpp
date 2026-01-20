#pragma once
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cwctype>
#include <algorithm>
#include <windows.h>
#include <filesystem>
#include "CommandGenerator.hpp"
#include "RspFileGenerator.hpp"
#include "ProjectModel.hpp"
#include "ProcessRunGuard.h"
#include "ThreadPool.h"
#include "Task.h"
#include "HeaderHelpers.h"

using namespace std::filesystem;

namespace cmakeparser {

	static inline bool CreateDirectoryW(const std::wstring path) {
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(path), ec);
		return !ec;
	};

	struct Command {
		std::wstring name;
		std::wstring raw_args;
		std::vector<std::wstring> args;
		int line_start = -1;
		int line_end = -1;

	public:
		const std::wstring GenerateCommand() {

		};
	};

	class AST {
	public:
		void AddCommand(Command&& cmd) { commands_.push_back(std::move(cmd)); }
		const std::vector<Command>& Commands() const { return commands_; }
	private:
		std::vector<Command> commands_;
	};


	class CmakeParser {
	public:
		explicit CmakeParser(bool clearBuild, std::function<void(const std::wstring&, const std::wstring& logFile, bool, bool)> callback = nullptr) : callback_(callback), clearDir_(clearBuild) {
		}
		bool Parse(const std::wstring& path)
		{
			Reset();

			std::string utf8;
			{
				std::ifstream ifs(path, std::ios::binary);
				if (!ifs) {
					last_error_ = L"Cannot open file";
					return false;
				}
				std::ostringstream ss;
				ss << ifs.rdbuf();
				utf8 = ss.str();
			}

			text_ = Utf8ToWide(utf8);

			if (text_.empty()) {
				last_error_ = L"UTF-8 decode failed";
				return false;
			}

			pos_ = 0;
			line_ = 1;
			n_ = (int)text_.size();

			ParseCommands();
			BuildModel();

			if (clearDir_) {
				auto& dir = GetBuildPath();
				std::filesystem::remove_all(dir);
				std::filesystem::create_directories(dir);
			}

			SetBaseDir(basePath_);

			return true;
		}

		void ConsoleLog(std::wostream& os = std::wcout) const {
			os << L"CMake parse result\n";
			for (const auto& c : ast_.Commands()) {
				os << L"[" << c.name << L"] ";
				for (auto& a : c.args) os << a << L" ";
				os << L"\n";
			}
		}

		int Build(const bool isFullLog, const std::wstring& logFile) {
			logFile_ = logFile;
			if (std::filesystem::exists(logFile)) {
				logFileHandle_ = CreateFileW(logFile_.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			}
			else {
				logFileHandle_ = CreateFileW(std::wstring(GetBuildPath() + L"/build.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			}
			auto result = GetModel();
			auto commands = GetAST();
			RspFileGenerator rspGenerator(result, GetRspPath());
			CommandGenerator generator(result, GetBasePath() + L"/NinjaBuilder/tools/gcc-arm-none-eabi/bin/", GetM3Path() + L"/src/mdk-arm/", GetObjPath());
			auto objcopy = GetBasePath() + L"/tools/gcc-arm-none-eabi/bin/arm-none-eabi-objcopy.exe";
			ProcessRunGuard guard;

			int ErrCode = 0;
			std::vector<Task<ProcessRunGuardResult>> tasks;
			std::atomic<size_t> indexBuild = (1);
			std::vector<std::wstring> commands_;

			while (generator.HasNext()) {
				std::wstring command;
				std::wstring rspFile;
				if (rspGenerator.CreateNextRspFile(rspFile)) {
					generator.Next(command, rspFile);
					commands_.push_back(command);
				}
			}
			for (auto command : commands_) {
				Task<ProcessRunGuardResult> task(
					[this, &guard, command](CancellationToken tok) {
						ProcessRunGuardResult result;

						if (tok && tok->load()) {
							result.code = -1;
							return result;
						}

						guard.RunCommand(command, result);
						return result;
					},
					0
				);

				task.Start();

				task.ContinueWith([this, &ErrCode,&tasks,&isFullLog,&indexBuild,&commands_](ProcessRunGuardResult r) {
					auto index = indexBuild.load(std::memory_order_acquire);
					indexBuild.fetch_add(1, std::memory_order_relaxed);
					if (r.code != 0) {

						ErrCode = r.code;
						CloseAll(tasks);
					}
					else {
						if (!r.stderrText.empty()) {
							SetConsole(r.stderrText, r.stderrText, false);

							if (r.code != 0) {
								std::wstringstream ss;
								ss << L"Failed with exit code: " << r.code << L"\n";
								SetConsole(ss.str(), ss.str(), false);
								return;
							}
						}

						std::wstringstream ss;
						ss << L"[" << index << L" /" << commands_.size() << L"] " << r.command;

						if (isFullLog) {
							SetConsole(ss.str(), ss.str());
						}
						else {
							std::wstringstream ss1;
							ss1 << L"[" << index << L" /" << commands_.size() << L"] " << L" успешно!";
							SetConsole(ss1.str(), ss.str());
						}
					}
					});


				tasks.push_back(std::move(task));
			}

			WaitAll(tasks);

			if (ErrCode != 0) {
				if (logFileHandle_ != INVALID_HANDLE_VALUE) {
					CloseHandle(logFileHandle_);
				}
				return ErrCode;
			}

			Task<int> task([&guard,this, &rspGenerator, &generator, isFullLog] {
				SetConsole(L"Создание elf...", L"Создание elf...");

				auto pathElf = GetBuildPath() + L"/MAIN.elf";
				auto pathBin = GetBuildPath() + L"/MAIN.bin";
				auto pathLinkFile = rspGenerator.CreateLinkRspFile(generator.GetLinks());
				auto command = generator.CreateLinkCommand(pathLinkFile, pathElf);
				auto commandBin = generator.CreateBinCommand(pathElf, pathBin);
				{
					ProcessRunGuardResult result;
					guard.RunCommand(command, result);

					if (result.seccess) {
						if (isFullLog) {
							SetConsole(result.command, result.command);
							SetConsole(L"Elf успешно создан!", L"Elf успешно создан!");
						}
						else {
							SetConsole(L"Elf успешно создан!", result.command, true, true);
						}
					}
					else {
						SetConsole(result.stderrText, result.stderrText, false);
						if (logFileHandle_ != INVALID_HANDLE_VALUE) {
							CloseHandle(logFileHandle_);
						}
						return (int)result.code;
					}
				}

				{
					ProcessRunGuardResult result;
					guard.RunCommand(commandBin, result);

					if (result.seccess) {
						if (isFullLog) {
							SetConsole(result.command, result.command);
							SetConsole(L"Bin успешно создан!", result.command, true, true);
						}
						else {
							SetConsole(L"Bin успешно создан!", result.command, true, true);
						}
					}
					else {
						SetConsole(result.stderrText, result.stderrText, false);
						if (logFileHandle_ != INVALID_HANDLE_VALUE) {
							CloseHandle(logFileHandle_);
						}
						return (int)result.code;
					}
				}

				return 0;
				});

			task.Start();

			Task<void> clear([&tasks, &commands_] {
				tasks.clear();
				commands_.clear();
				});
			clear.Start();

			for (size_t i = 0; i < 100; i++)
			{
				Task<void> task([i] {
					auto res = i * i;
					});

				task.ContinueWith([&] {
					auto res = task.IsReady();
					});

				task.Start();
			}

			return task.Get();
		}

		const std::wstring& GetBasePath() const { return basePath_; }
		const std::wstring& GetM3Path() const { return m3Path_; }
		const std::wstring& GetBuildPath() const { return buildPath_; }
		const std::wstring& GetObjPath() const { return objPath_; }
		const std::wstring& GetRspPath() const { return rspPath_; }
		const AST& GetAST() const { return ast_; }
		const ProjectModel& GetModel() const { return model_; }

	private:
		std::wstring basePath_;
		std::wstring m3Path_;
		std::wstring buildPath_;
		std::wstring objPath_;
		std::wstring rspPath_;
		std::wstring text_;
		int pos_ = 0;
		int n_ = 0;
		int line_ = 1;
		bool clearDir_;

		AST ast_;
		ProjectModel model_;
		std::wstring last_error_;

		HANDLE logFileHandle_ = INVALID_HANDLE_VALUE;
		std::wstring logFile_;
		std::mutex g_logMutex_;
		std::function<void(const std::wstring&, const std::wstring&, bool, bool)> callback_;

	private:

		void SetBaseDir(const std::wstring& path) {
			basePath_ = path;
			m3Path_ = basePath_ + L"/NinjaBuilder/M3_CITY2";
			buildPath_ = m3Path_ + L"/Build";

			std::filesystem::create_directories(buildPath_);
			rspPath_ = buildPath_ + L"/rsp";
			objPath_ = buildPath_ + L"/obj";
			if (std::filesystem::exists(rspPath_))
				std::filesystem::remove_all(rspPath_);

			if (std::filesystem::exists(objPath_))
				std::filesystem::remove_all(objPath_);

			std::filesystem::create_directories(rspPath_);
			std::filesystem::create_directories(objPath_);
		};

		void SetConsole(const std::wstring& text, const std::wstring& fullText, bool seccuses = true, bool repeat = false) {
			if (callback_ == nullptr) {
				std::wcout << text << L"\n";
				LogAppend(fullText.c_str());
				if (repeat)
					LogAppend(text.c_str());
			}
			else {
				callback_(text, fullText, seccuses, repeat);
			}
		}

		void LogAppend(
			const wchar_t* message)
		{
			if (logFileHandle_ == INVALID_HANDLE_VALUE)
				return;

			std::lock_guard<std::mutex> lk(g_logMutex_);

			LARGE_INTEGER zero{};
			if (!SetFilePointerEx(logFileHandle_, zero, nullptr, FILE_END))
				return;

			auto out = stringHelper::ToStringBestEffort(message);
			if (out.empty())
				return;

			DWORD written = 0;
			if (!WriteFile(logFileHandle_,
				out.data(),
				static_cast<DWORD>(out.size()),
				&written,
				nullptr) ||
				written != out.size())
			{
				// ошибка записи
			}
		}

		void Reset() {
			text_.clear();
			ast_ = {};
			model_ = {};
			pos_ = 0;
			line_ = 1;
		}

		void SkipSpaces() {
			while (pos_ < n_ && iswspace(text_[pos_])) {
				if (text_[pos_] == L'\n') line_++;
				pos_++;
			}
		}

		std::wstring Utf8ToWide(const std::string& s) {
			if (s.empty()) return {};

			int size = MultiByteToWideChar(
				CP_UTF8,
				0,
				s.data(),
				(int)s.size(),
				nullptr,
				0
			);

			std::wstring out(size, 0);
			MultiByteToWideChar(
				CP_UTF8,
				0,
				s.data(),
				(int)s.size(),
				out.data(),
				size
			);

			return out;
		}

		bool IsIdentStart(wchar_t c) const {
			return iswalpha(c) || c == L'_';
		}

		bool IsIdent(wchar_t c) const {
			return iswalnum(c) || c == L'_' || c == L'-';
		}

		std::wstring ReadIdent() {
			std::wstring r;
			while (pos_ < n_ && IsIdent(text_[pos_])) {
				r += towlower(text_[pos_++]);
			}
			return r;
		}

		std::wstring ReadParenBlock() {
			std::wstring r;
			int depth = 1;
			while (pos_ < n_ && depth > 0) {
				wchar_t c = text_[pos_++];
				if (c == L'(') depth++;
				else if (c == L')') {
					depth--;
					if (depth == 0) break;
				}
				r += c;
			}
			return r;
		}

		std::vector<std::wstring> SplitArgs(const std::wstring& s) {
			std::vector<std::wstring> out;
			std::wstring cur;
			bool quoted = false;

			for (wchar_t c : s) {
				if (c == L'"') {
					quoted = !quoted;
					continue;
				}
				if (!quoted && iswspace(c)) {
					if (!cur.empty()) {
						out.push_back(cur);
						cur.clear();
					}
				}
				else cur += c;
			}
			if (!cur.empty()) out.push_back(cur);
			return out;
		}

		void ParseCommands() {
			while (pos_ < n_) {
				SkipSpaces();
				if (pos_ >= n_) break;

				if (text_[pos_] == L'#') {
					while (pos_ < n_ && text_[pos_] != L'\n') pos_++;
					continue;
				}

				if (!IsIdentStart(text_[pos_])) {
					pos_++;
					continue;
				}

				int ls = line_;
				auto name = ReadIdent();
				SkipSpaces();

				if (pos_ < n_ && text_[pos_] == L'(') {
					pos_++;
					auto body = ReadParenBlock();
					Command c;
					c.name = name;
					c.raw_args = body;
					c.args = SplitArgs(body);
					c.line_start = ls;
					c.line_end = line_;
					ast_.AddCommand(std::move(c));
				}
			}
		}

		size_t count = 0;

		void BuildModel() {
			for (auto& c : ast_.Commands()) {
				if (c.name == L"project" && !c.args.empty())
					model_.AddProject(c.args[0], basePath_);
				else if (c.name == L"set" && c.args.size() > 1)
				{
					model_.AddSet(c.args[0], { c.args.begin() + 1, c.args.end() });
					if (c.args[0] == L"BASE_DIR") {
						SetBaseDir(c.args[1]);
					}
					else if (c.args[0] == L"CMAKE_C_FLAGS") {
						model_.AddCompileFlags(c.args[1]);
					}
					else if (c.args[0] == L"CMAKE_EXE_LINKER_FLAGS") {
						std::wstring out;
						if (NormalizePath(c.args[1], GetM3Path(), out)) {
							model_.AddLinkTFlags(out);
						}
						else {
							model_.AddLinkFlags(c.args[1]);
						}
					}
					else if (c.args[0] == L"CMAKE_ASM_FLAGS") {
						model_.AddAsmFlags(c.args[1]);
					}
					else if (c.raw_args.size() >= 3 && c.raw_args.substr(0, 3) == L"SRC") {
						for (int i = 1; i < c.args.size(); i++) {
							model_.AddSrc(c.args[i], basePath_);
						}
					}
				}
				else if ((c.name == L"add_executable" || c.name == L"add_library") && c.args.size() > 1)
					model_.AddTarget(c.args[0], { c.args.begin() + 1, c.args.end() });
				else if (c.name == L"include_directories") {
					std::wstring name = L"";
					for (size_t i = 0; i < c.args.size(); ++i) {
						name += c.args[i];
						if (i + 1 < c.args.size())
							name += L" ";
					}
					model_.AddIncludeDir(name, basePath_);
				}

				count++;
				if (count == 64)
				{
					count++;
				}
			}
		}

		bool NormalizePath(const std::wstring& d, const std::wstring baseDir, std::wstring& out) const {
			std::wstring result = d;
			const std::wstring token = L"${CMAKE_SOURCE_DIR}";
			size_t pos = 0;
			bool find = false;
			std::wstring outres;
			while ((pos = result.find(token, pos) != std::wstring::npos)) {
				outres.replace(pos - 1, token.length(), baseDir);
				pos += baseDir.length();
				std::wstring name;
				find = ExtractFileName(d, name);
				outres += L"/" + name;
				out = outres;
				break;
			}
			return find;
		}

		bool ExtractFileName(const std::wstring& s, std::wstring& out) const {
			out.clear();
			if (s.empty()) return false;

			size_t tPos = s.find(L"-T");
			if (tPos == std::wstring::npos) return false;

			size_t i = tPos + 2;
			while (i < s.size() && std::iswspace(static_cast<wint_t>(s[i]))) ++i;
			if (i >= s.size()) return false;

			while (i < s.size() && (s[i] == L'\\' || s[i] == L'"' || s[i] == L'\'')) ++i;
			if (i >= s.size()) return false;

			size_t start = i;
			size_t end = start;
			while (end < s.size() && !std::iswspace(static_cast<wint_t>(s[end]))) ++end;

			if (end <= start) return false;

			while (end > start && (s[end - 1] == L'\\' || s[end - 1] == L'"' || s[end - 1] == L'\'')) --end;
			if (end <= start) return false;

			std::wstring fullPath = s.substr(start, end - start);
			size_t lastSlash = fullPath.find_last_of(L"/\\");
			if (lastSlash == std::wstring::npos) {
				size_t rb = fullPath.find_last_of(L'}');
				if (rb != std::wstring::npos && rb + 1 < fullPath.size()) {
					out = fullPath.substr(rb + 1);
				}
				else {
					out = fullPath;
				}
			}
			else {
				out = fullPath.substr(lastSlash + 1);
			}

			if (!out.empty() && (out.front() == L'"' || out.front() == L'\'')) out.erase(out.begin());
			while (!out.empty() && (out.back() == L'\\' || out.back() == L'"' || out.back() == L'\'')) out.pop_back();

			return !out.empty();
		}
	};

}