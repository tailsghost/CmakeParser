#pragma once

#include <filesystem>
#include "ProjectModel.hpp"

namespace cmakeparser {

	class RspFileGenerator
	{
	public:
		explicit RspFileGenerator(const ProjectModel& model, const std::wstring& rspDir) : rspDir_(rspDir), model_(model) {
			
		}

		bool CreateNextRspFile(std::wstring& rspFile) {
			auto fileName = std::filesystem::path(model_.GetSrcPathC(index_)).filename().wstring();
			rspFile = rspDir_ + L"/" + fileName + L".obj_compile.rsp";
			std::ofstream  ofs(rspFile, std::ios::out | std::ios::trunc);
			if (!ofs) {
				std::wcerr << L"Cannot create file: " << rspFile << L"\n";
				return false;
			}

			const auto& flags = model_.Flags();
			const auto& inc = model_.IncludeDirs();

			for (auto& c : flags) {
				std::string line = ToAnsi(c) + "\n";
				ofs.write(line.c_str(), line.size());
			}

			for (auto& d : inc) {
				std::string line = ToAnsi(quote_w(L"-I" + d)) + "\n";
				ofs.write(line.c_str(), line.size());
			}

			index_++;
			return true;
		}

		const std::wstring CreateLinkRspFile(std::vector<std::wstring> links) {
			auto rspPath = rspDir_ + L"/link.rsp";
			std::ofstream  ofs(rspPath, std::ios::out | std::ios::trunc);
			if (!ofs) {
				std::wcerr << L"Cannot create file: " << rspPath << L"\n";
				return L"";
			}
			const auto& flagsT = model_.LinkTFlags();
			const auto& flagsAsm = model_.LinkAsmFlags();
			const auto& flags = model_.LinkFlags();
			for (auto& c : flagsT) {
				std::string line = "-Wl,-T " + ToAnsi(quote_w(c)) + "\n";
				ofs.write(line.c_str(), line.size());
			}
			for (auto link : links)
			{
				std::string line = ToAnsi(quote_w(link)) + "\n";
				ofs.write(line.c_str(), line.size());
			}
			for (auto& c : flagsAsm) {
				std::string line = ToAnsi(c) + "\n";
				ofs.write(line.c_str(), line.size());
			}
			for (auto& c : flags) {
				std::string line = ToAnsi(c) + "\n";
				ofs.write(line.c_str(), line.size());
			}

			return rspPath;
		}

	private:
		const std::wstring rspDir_;
		const ProjectModel& model_;

		size_t index_ = 0;

		std::string ToUtf8(const std::wstring& w) {
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

		std::string ToAnsi(const std::wstring& wstr)
		{
			if (wstr.empty()) return {};
			int size_needed = WideCharToMultiByte(1251, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
			std::string strTo(size_needed, 0);
			WideCharToMultiByte(1251, 0, wstr.c_str(), (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
			return strTo;
		}
		std::wstring quote_w(const std::wstring& s) {
			if (s.find_first_of(L" \t\"") == std::wstring::npos) return s;
			std::wstring res = L"\"";
			for (wchar_t c : s) {
				if (c == L'"') res += L"\\\""; else res.push_back(c);
			}
			res += L"\"";
			return res;
		}
	};
}

