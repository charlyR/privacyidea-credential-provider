#pragma once
#include <string>
#include <vector>

class Convert
{
public:
	static std::wstring ToWString(const std::string& s);
	static std::string ToString(const std::wstring& ws);
	static std::string ToString(const bool b);
	static std::wstring ToUpperCase(std::wstring s);
	static std::string ToUpperCase(std::string s);
	static std::string LongToHexString(long in);
	static std::wstring JoinW(const std::vector<std::wstring>& elements, const wchar_t* const separator);
};
