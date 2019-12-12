/* * * * * * * * * * * * * * * * * * * * *
**
** Copyright	2019 NetKnights GmbH
** Author:		Nils Behlen
**
**    Licensed under the Apache License, Version 2.0 (the "License");
**    you may not use this file except in compliance with the License.
**    You may obtain a copy of the License at
**
**        http://www.apache.org/licenses/LICENSE-2.0
**
**    Unless required by applicable law or agreed to in writing, software
**    distributed under the License is distributed on an "AS IS" BASIS,
**    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**    See the License for the specific language governing permissions and
**    limitations under the License.
**
** * * * * * * * * * * * * * * * * * * */

#include "OfflineHandler.h"
#include "Endpoint.h"
#include <iostream>
#include <fstream>
#include <atlenc.h>

#pragma comment (lib, "bcrypt.lib")

using namespace std;
using json = nlohmann::json;

OfflineHandler::OfflineHandler(string filePath, int tryWindow)
{
	// Load the offline file on startup
	_filePath = filePath;
	_tryWindow = tryWindow;
	if (loadFromFile() != S_OK)
	{
	}
}

OfflineHandler::~OfflineHandler()
{
	HRESULT res = saveToFile();
	if (res != S_OK)
	{
		//cout << "Error while saving: " << res << endl;
	}
}

HRESULT OfflineHandler::verifyOfflineOTP(wstring otp, string username)
{
	HRESULT success = E_FAIL;

	for (auto& item : dataSets)
	{
		if (item.user == username || item.username == username)
		{
			int lowestKey = item.getLowestKey();
			int matchingKey = lowestKey;

			for (int i = lowestKey; i < (lowestKey + _tryWindow); i++)
			{
				try
				{
					string storedValue = item.offlineOTPs.at(to_string(i));
					if (pbkdf2_sha512_verify(otp, storedValue))
					{
						matchingKey = i;
						success = S_OK;
						break;
					}
				}
				catch (std::out_of_range & e)
				{
					// TODO nothing, just skip?
				}
			}

			if (success == S_OK)
			{
				if (matchingKey >= lowestKey) // Also include if the matching is the first
				{
					cout << "difference: " << (matchingKey - lowestKey) << endl;
					for (int i = lowestKey; i <= matchingKey; i++)
					{
						item.offlineOTPs.erase(to_string(i));
					}
				}
			}
		}
	}

	return success;
}

int OfflineHandler::getOfflineValuesLeft(std::string username)
{
	if (dataSets.empty()) return -1;

	for (auto& item : dataSets)
	{
		if (item.user == username || item.username == username)
		{
			return item.offlineOTPs.size();
		}
	}

	return -1;
}

HRESULT OfflineHandler::getRefillTokenAndSerial(std::string username, __out std::map<std::string, std::string>& map)
{
	if (dataSets.empty()) return OFFLINE_NO_OFFLINE_DATA;

	for (auto& item : dataSets)
	{
		if (item.user == username || item.username == username)
		{
			string serial = item.serial;
			string refilltoken = item.refilltoken;
			if (serial.empty() || refilltoken.empty()) return OFFLINE_NO_OFFLINE_DATA;
			map.try_emplace("serial", serial);
			map.try_emplace("refilltoken", refilltoken);
			return S_OK;

		}
	}

	return OFFLINE_DATA_USER_NOT_FOUND;
}

/* Check a authentication reponse from privacyIDEA if it contains the inital data for offline */
HRESULT OfflineHandler::parseForOfflineData(std::string in)
{
	if (in.empty()) return E_FAIL;

	json j;
	try
	{
		j = json::parse(in);
	}
	catch (json::parse_error & e)
	{
		return OFFLINE_JSON_PARSE_ERROR;
	}

	auto jAuth_items = j["auth_items"];
	if (jAuth_items == NULL) return OFFLINE_NO_OFFLINE_DATA;

	// Get the serial to add to the data
	auto jSerial = j["detail"]["serial"];
	if (!jSerial.is_string()) return OFFLINE_JSON_FORMAT_ERROR;
	string serial = jSerial.get<std::string>();

	auto jOffline = jAuth_items["offline"];

	if (!jOffline.is_array()) return OFFLINE_JSON_FORMAT_ERROR;
	if (jOffline.size() < 1) return OFFLINE_NO_OFFLINE_DATA;

	for (auto& item : jOffline)
	{
		OfflineData d(item.dump());
		d.serial = serial;
		dataSets.push_back(d);
	}
	return S_OK;
}

HRESULT OfflineHandler::parseRefillResponse(std::string in, std::string username)
{
	json jResponse;
	try
	{
		jResponse = json::parse(in);
	}
	catch (json::parse_error & e)
	{
		//cout << e.what() << " at " << __LINE__ << endl;
		return OFFLINE_JSON_PARSE_ERROR;
	}

	// Set the new refill token
	json offline;
	try
	{
		offline = jResponse["auth_items"]["offline"].at(0);
	}
	catch (const std::exception & e)
	{
		//cout << e.what() << " at " << __LINE__ << endl;
		return OFFLINE_JSON_FORMAT_ERROR;
	}

	if (offline == nullptr) return OFFLINE_JSON_FORMAT_ERROR;

	for (auto& item : dataSets)
	{
		if (item.user == username || item.username == username)
		{
			// TODO if there is no refill token then what? 
				// still adding the values we got
			if (offline["refilltoken"].is_string())
			{
				item.refilltoken = offline["refilltoken"].get<std::string>();
			}
			else
			{
				item.refilltoken = "";
			}

			auto jResponse = offline["response"];
			for (auto& jItem : jResponse.items())
			{
				string key = jItem.key();
				string value = jItem.value();
				item.offlineOTPs.try_emplace(key, value);
			}
			return S_OK;
		}
	}

	return E_FAIL;
}

HRESULT OfflineHandler::getLastErrorCode()
{
	return _lastError;
}

HRESULT OfflineHandler::isDataVailable(std::string username)
{
	// Check is usable data available for the given username
	for (auto& item : dataSets)
	{
		if (item.user == username || item.username == username)
		{
			return (item.offlineOTPs.empty() ? OFFLINE_DATA_NO_OTPS_LEFT : S_OK);
		}
	}

	return OFFLINE_DATA_USER_NOT_FOUND;
}

HRESULT OfflineHandler::saveToFile()
{
	ofstream o;
	o.open(_filePath, ios_base::out); // Destroy contents | create new

	if (!o.is_open()) return GetLastError();

	json::array_t jArr;

	for (auto& item : dataSets)
	{
		jArr.push_back(item.toJSON());
	}

	json j;
	j["offline"] = jArr;

	o << j.dump(4);
	o.close();
	return S_OK;
}

HRESULT OfflineHandler::loadFromFile()
{
	// Check for the file, load if exists
	string fileContent = "";
	string line;
	ifstream ifs(_filePath);

	if (!ifs.good()) return OFFLINE_FILE_DOES_NOT_EXIST;

	if (ifs.is_open())
	{
		while (getline(ifs, line))
		{
			fileContent += line;
		}
		ifs.close();
	}

	if (fileContent.empty()) return OFFLINE_FILE_EMPTY;

	try
	{
		auto j = json::parse(fileContent);

		auto jOffline = j["offline"];

		if (jOffline.is_array())
		{
			for (auto& item : jOffline)
			{
				OfflineData d(item.dump());
				dataSets.push_back(d);
			}
		}
	}
	catch (json::parse_error & e)
	{
		//cout << e.what() << " at " << __LINE__ << endl;
		return OFFLINE_JSON_PARSE_ERROR;
	}

	return S_OK;
}

// 65001 is utf-8.
wchar_t* OfflineHandler::CodePageToUnicode(int codePage, const char* src)
{
	if (!src) return 0;
	int srcLen = strlen(src);
	if (!srcLen)
	{
		wchar_t* w = new wchar_t[1];
		w[0] = 0;
		return w;
	}

	int requiredSize = MultiByteToWideChar(codePage,
		0,
		src, srcLen, 0, 0);

	if (!requiredSize)
	{
		return 0;
	}

	wchar_t* w = new wchar_t[requiredSize + 1];
	w[requiredSize] = 0;

	int retval = MultiByteToWideChar(codePage,
		0,
		src, srcLen, w, requiredSize);
	if (!retval)
	{
		delete[] w;
		return 0;
	}

	return w;
}

std::string OfflineHandler::getNextValue(std::string& in)
{
	string tmp = in.substr(in.find_last_of('$') + 1);
	in = in.substr(0, in.find_last_of('$'));
	return tmp;
}

char* OfflineHandler::UnicodeToCodePage(int codePage, const wchar_t* src)
{
	if (!src) return 0;
	int srcLen = wcslen(src);
	if (!srcLen)
	{
		char* x = new char[1];
		x[0] = '\0';
		return x;
	}

	int requiredSize = WideCharToMultiByte(codePage,
		0,
		src, srcLen, 0, 0, 0, 0);

	if (!requiredSize)
	{
		return 0;
	}

	char* x = new char[requiredSize + 1];
	x[requiredSize] = 0;

	int retval = WideCharToMultiByte(codePage,
		0,
		src, srcLen, x, requiredSize, 0, 0);
	if (!retval)
	{
		delete[] x;
		return 0;
	}

	return x;
}

bool OfflineHandler::pbkdf2_sha512_verify(wstring password, string storedValue)
{
	bool isValid = false;

	// Format of stored values (passlib):
	// $algorithm$iteratons$salt$checksum
	string storedOTP = getNextValue(storedValue);
	// $algorithm$iteratons$salt
	string salt = getNextValue(storedValue);
	// $algorithm$iteratons
	int iterations = 1000; // TODO default useful??
	try {
		iterations = stoi(getNextValue(storedValue));
	}
	catch (invalid_argument & e) {
		//cout << e.what() << " at " << __LINE__ << endl;
	}
	// $algorithm
	string algorithm = getNextValue(storedValue);

	// Salt is in adapted abase64 encoding of passlib where [./+] is substituted
	//cout << "salt before substitution: " << salt << endl;
	base64toabase64(salt);
	//cout << "salt after substitution: " << salt << endl;

	int bufLen = Base64DecodeGetRequiredLength(salt.size() + 1);
	BYTE* bufSalt = (BYTE*)CoTaskMemAlloc(bufLen);
	if (bufSalt == NULL) return false;
	Base64Decode(salt.c_str(), (salt.size() + 1), bufSalt, &bufLen);
	//printBytes("salt bytes", bufSalt, bufLen);

	// The password is encoded into UTF-8 from Unicode
	char* readyPassword = UnicodeToCodePage(65001, password.c_str());

	int readyPwSize = strlen(readyPassword);

	BYTE* readyPasswordBytes = reinterpret_cast<unsigned char*>(readyPassword);
	//printBytes("password bytes", readyPasswordBytes, readyPwSize);

	// Get the size of the output from the stored value, which is also in abase64 encoding
	//cout << "stored otp size " << storedOTP.size() << endl;
	//cout << "stored otp before substitution: " << storedOTP << endl;
	replace(storedOTP.begin(), storedOTP.end(), '.', '+');
	//cout << "stored otp after substitution: " << storedOTP << endl;

	int bufLenStored = Base64DecodeGetRequiredLength(storedOTP.size() + 1);
	BYTE* bufStored = (BYTE*)CoTaskMemAlloc(bufLenStored);
	if (bufStored == NULL) return false;
	Base64Decode(storedOTP.c_str(), storedOTP.size() + 1, bufStored, &bufLenStored);
	//printBytes("stored checksum bytes", bufStored, bufLenStored);
	//cout << "checksum byte count: " << bufLenStored << endl;

	// DO PBKDF2
	ULONG cbStored = storedOTP.size() + 1;
	ULONGLONG cIterations = iterations;
	ULONG cbDerivedKey = (ULONG)bufLenStored;
	PUCHAR pbDerivedKey = (unsigned char*)CoTaskMemAlloc(sizeof(unsigned char) * cbDerivedKey);
	if (pbDerivedKey == NULL)
	{
		//cout << "could not allocate memory" << endl;
		return false;
	}

	ULONG dwFlags = 0; // RESERVED, MUST BE ZERO
	BCRYPT_ALG_HANDLE hPrf = BCRYPT_HMAC_SHA512_ALG_HANDLE;

	NTSTATUS status = BCryptDeriveKeyPBKDF2(hPrf, readyPasswordBytes, readyPwSize, bufSalt, bufLen, cIterations, pbDerivedKey, cbDerivedKey, dwFlags);

	CoTaskMemFree(bufSalt);

	if (status == 0)
	{
		//printBytes("derived key bytes", pbDerivedKey, cbDerivedKey);

		// Compare the bytes
		if (cbDerivedKey == bufLenStored)
		{
			while (cbDerivedKey--)
			{
				////cout << +pbDerivedKey[cbDerivedKey] << " ";
				if (pbDerivedKey[cbDerivedKey] != bufStored[cbDerivedKey])
				{
					CoTaskMemFree(pbDerivedKey);
					CoTaskMemFree(bufStored);
					return false;
				}
			}
			////cout << endl;
			isValid = true;
		}
	}
	else
	{
		printf("Error: %x", status);
		isValid = false;
	}

	CoTaskMemFree(pbDerivedKey);
	CoTaskMemFree(bufStored);

	return isValid;
}

void OfflineHandler::base64toabase64(std::string& in)
{
	std::replace(in.begin(), in.end(), '.', '+');
}
