#include "CmakeParser.hpp"
#include <io.h>
#include <fcntl.h>
#include <iostream>
#include <string>

using namespace cmakeparser;

int wmain(int argc, wchar_t* argv[])
{
    int oldOutMode = _setmode(_fileno(stdout), _O_U16TEXT);

    std::wstring cmakePath;
    bool isFullLog = false;

    for (int i = 1; i < argc; ++i)
    {
        std::wstring arg = argv[i];
        if (arg == L"-full")
        {
            isFullLog = true;
        }
        else if (cmakePath.empty())
        {
            cmakePath = arg;
        }
    }

    if (cmakePath.empty())
    {
        std::wcout << L"Введите путь к CMakeLists.txt:\n";
        std::getline(std::wcin, cmakePath);

        std::wstring useLogs;
        std::wcout << L"Использовать полный вывод для отладки? 1 - да, любое другое - нет: ";
        std::getline(std::wcin, useLogs);
        isFullLog = (useLogs == L"1");
    }

    if (cmakePath.empty())
    {
        std::wcerr << L"Ошибка: пустой путь!\n";
        _setmode(_fileno(stdout), oldOutMode);
        return -1;
    }

    CmakeParser parser(true);
    parser.Parse(cmakePath);
    auto result = parser.Build(isFullLog, L"");
    std::wcout << L"Очищаем консоль.....\n";
    std::wcout.flush();
    _setmode(_fileno(stdout), oldOutMode);
    TerminateProcess(GetCurrentProcess(), (UINT)result);
    return result;
}