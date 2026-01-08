#pragma once

namespace cmakeparser {

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
			while (ss >> word) compile_flags_.push_back(word);
		}

		void AddAsmFlags(const std::wstring& f) {
			std::wstringstream ss(f);
			std::wstring word;
			while (ss >> word) linkAsmFlag_.push_back(word);
		}

		void AddLinkFlags(const std::wstring& f) {
			std::wstringstream ss(f);
			std::wstring word;
			while (ss >> word) linkFlag_.push_back(word);
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
			static const std::wstring empty;
			if (src_.size() > index) return src_[index];
			return empty;
		}

		size_t SrcCount() const { return src_.size(); }

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
			while ((pos = result.find(token, pos)) != std::wstring::npos) {
				result.replace(pos, token.length(), baseDir);
				pos += baseDir.length();
			}
			return result;
		}
	};
}