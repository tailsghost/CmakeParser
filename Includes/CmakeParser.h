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

	class ProjectModel {
	public:
		void AddIncludeDir(const std::wstring& d, const std::wstring baseDir) {
			include_dirs_.push_back(NormalizePath(d, baseDir));
		}
		void AddSet(const std::wstring& k, const std::vector<std::wstring>& v) { sets_[k] = v; }
		void AddTarget(const std::wstring& t, const std::vector<std::wstring>& s) { targets_[t] = s; }
		void AddProject(const std::wstring& p, const std::wstring baseDir) { projects_.push_back(NormalizePath(p, baseDir)); }
		void AddSrc(const std::wstring& p, const std::wstring baseDir) { src_.push_back(NormalizePath(p, baseDir)); }
		void AddCompileFlags(const std::wstring& f) {
			std::wstringstream ss(f);
			std::wstring word;
			while (ss >> word) {
				compile_flags_.push_back(word);
			};
		}

		void AddAsmFlags(const std::wstring& f) {
			std::wstringstream ss(f);
			std::wstring word;
			while (ss >> word) {
				linkAsmFlag_.push_back(word);
			};
		}

		void AddLinkFlags(const std::wstring& f) {
			std::wstringstream ss(f);
			std::wstring word;
			while (ss >> word) {
				linkFlag_.push_back(word);
			};
		}

		void AddLinkTFlags(const std::wstring& f) {
			linkTFlag_.push_back(f);
		}

		const auto& IncludeDirs() const { return include_dirs_; }
		const auto& Sets() const { return sets_; }
		const auto& Targets() const { return targets_; }
		const auto& Projects() const { return projects_; }
		const auto& Flags() const { return compile_flags_; }
		const auto& LinkFlags() const { return linkFlag_; }
		const auto& LinkTFlags() const { return linkTFlag_; }
		const auto& LinkAsmFlags() const { return linkAsmFlag_; }

		const std::wstring& GetSrcPathC(size_t index) const {
			const std::wstring empty;
			if (src_.size() > index) {
				return src_[index];
			}
			return empty;
		}

		const size_t SrcCount() const {
			return src_.size();
		}

	private:
		std::vector<std::wstring> include_dirs_;
		std::map<std::wstring, std::vector<std::wstring>> sets_;
		std::map<std::wstring, std::vector<std::wstring>> targets_;
		std::vector<std::wstring> projects_;
		std::vector<std::wstring> compile_flags_;
		std::vector<std::wstring> src_;
		std::vector<std::wstring> linkFlag_;
		std::vector<std::wstring> linkTFlag_;
		std::vector<std::wstring> linkAsmFlag_;

	private:
		std::wstring NormalizePath(const std::wstring& d, const std::wstring baseDir) const {
			std::wstring result = d;
			const std::wstring token = L"${BASE_DIR}";
			size_t pos = 0;
			while ((pos = result.find(token, pos) != std::wstring::npos)) {
				result.replace(pos - 1, token.length(), baseDir);
				pos += baseDir.length();
			}

			return result;
		}
	};

	class CmakeParser {
	public:
		explicit CmakeParser() = default;
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
			return true;
		}

		void ConsoleLog(std::wostream& os = std::wcout) const {
			os << L"CMake parse result\n";
			for (const auto& c : ast_.Commands()) {
				os << L"[" << c.name << L"] ";
				for (auto& a : c.args)
					os << a << L" ";
				os << L"\n";
			}
		}

		const std::wstring& GetBasePath() const { return basePath_; }
		const std::wstring& GetM3Path() const { return m3Path_; }
		const std::wstring& GetBuildPath() const { return buildPath_; }

		const AST& GetAST() const { return ast_; }
		const ProjectModel& GetModel() const { return model_; }

	private:
		std::wstring basePath_;
		std::wstring m3Path_;
		std::wstring buildPath_;

		std::wstring text_;
		int pos_ = 0;
		int n_ = 0;
		int line_ = 1;

		AST ast_;
		ProjectModel model_;
		std::wstring last_error_;

	private:

		void SetBaseDir(const std::wstring& path) {
			if (basePath_.empty())
			{
				basePath_ = path;
				m3Path_ = basePath_ + L"/NinjaBuilder/M3_CITY2";
				buildPath_ = m3Path_ + L"/Build";
			}
		};

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
						for (int i = 1; i < c.args.size();i++) {
							model_.AddSrc(c.args[i], basePath_);
						}
					}
				}
				else if ((c.name == L"add_executable" || c.name == L"add_library") && c.args.size() > 1)
					model_.AddTarget(c.args[0], { c.args.begin() + 1, c.args.end() });
				else if (c.name == L"include_directories") {
					model_.AddIncludeDir(c.args[0], basePath_);
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