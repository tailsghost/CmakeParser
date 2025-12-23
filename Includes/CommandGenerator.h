#pragma once

#include <string>
#include <vector>
#include <map>
#include <cwctype>

#include "CmakeParser.h"

namespace cmakeparser {

	class CommandGenerator {
	public:
		explicit CommandGenerator(
			const ProjectModel& model,
			const std::wstring& pathGcc, const std::wstring& pathArm)
			: model_(model), pathGcc_(pathGcc + L"arm-none-eabi-gcc.exe"), pathGObj_(pathGcc + L"arm-none-eabi-objcopy.exe"), pathArm_(pathArm)
		{
		}

		bool HasNext() const {
			return index_ < model_.SrcCount();
		}

		bool Next(std::wstring& out, std::wstring& pathRsp) {
			if (!HasNext())
				return false;

			auto file = model_.GetSrcPathC(index_++);
			std::wstring fileObjName = file + L".obj";
			out = quote_w(pathGcc_) +  L" @" + quote_w(pathRsp) + L" -MD -MT " + quote_w(fileObjName) + L" -MF " + quote_w(file + L".obj.d") + L" -o " + quote_w(file + L".obj") + L" -c " + quote_w(file);
			link_.push_back(fileObjName);
			return true;
		}

		std::wstring CreateLinkCommand(std::wstring link_rsp, std::wstring pathOutput) {
			return quote_w(pathGcc_) + L" @" + quote_w(link_rsp) + L" -o " + quote_w(pathOutput);
		}
		std::wstring CreateBinCommand(const std::wstring& pathInput, const std::wstring& pathOutput) {
			return quote_w(pathGObj_) + L" -Obinary " + quote_w(pathInput) + L" " + quote_w(pathOutput);
		}

		std::wstring CreateHexCommand(const std::wstring& pathInput, const std::wstring& pathOutput) {
			return quote_w(pathGObj_) + L" -Oihex " + quote_w(pathInput) + L" " + quote_w(pathOutput);
		}

		void Reset() {
			index_ = 0;
		}

		std::vector<std::wstring> GetLinks() { return link_; }

	private:
		const ProjectModel& model_;
		size_t index_ = 0;
		size_t indexLink_ = 0;
		const std::wstring pathGcc_;
		const std::wstring pathArm_;
		const std::wstring pathGObj_;

		std::wstring quote_w(const std::wstring& s) {
			if (s.find_first_of(L" \t\"") == std::wstring::npos) return s;
			std::wstring res = L"\"";
			for (wchar_t c : s) {
				if (c == L'"') res += L"\\\""; else res.push_back(c);
			}
			res += L"\"";
			return res;
		}

		std::vector<std::wstring> link_;


	};
}