/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
**
** Copyright	2012 Dominik Pretzsch
**				2017 NetKnights GmbH
**
** Author		Dominik Pretzsch
**				Nils Behlen
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
** * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef WIN32_NO_STATUS
#include <ntstatus.h>
#define WIN32_NO_STATUS
#endif

#include "CCredential.h"
#include "Configuration.h"
#include "Endpoint.h"
#include "Logger.h"
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <future>
#include <sstream>

using namespace std;

CCredential::CCredential()
{
	_cRef = 1;
	_pCredProvCredentialEvents = nullptr;

	DllAddRef();

	_dwComboIndex = 0;

	ZERO(_rgCredProvFieldDescriptors);
	ZERO(_rgFieldStatePairs);
	ZERO(_rgFieldStrings);
	auto& config = Configuration::Get();
	_endpoint = Endpoint(config.endpoint.hostname, config.endpoint.path, config.endpoint.customPort, config.endpoint.sslIgnoreCN, config.endpoint.sslIgnoreCA);
}

CCredential::~CCredential()
{
	General::Fields::Clear(_rgFieldStrings, _rgCredProvFieldDescriptors, this, NULL, CLEAR_FIELDS_ALL_DESTROY);
	DllRelease();
}

// Initializes one credential with the field information passed in.
// Set the value of the SFI_USERNAME field to pwzUsername.
// Optionally takes a password for the SetSerialization case.
HRESULT CCredential::Initialize(
	__in const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR* rgcpfd,
	__in const FIELD_STATE_PAIR* rgfsp,
	__in_opt PWSTR user_name,
	__in_opt PWSTR domain_name,
	__in_opt PWSTR password
)
{

	wstring wstrUsername, wstrDomainname, wstrPassword;

	if (NOT_EMPTY(user_name))
	{
		wstrUsername = wstring(user_name);
	}
	if (NOT_EMPTY(domain_name))
	{
		wstrDomainname = wstring(domain_name);
	}
	if (NOT_EMPTY(password))
	{
		wstrPassword = wstring(password);
	}

#ifdef _DEBUG
	DebugPrintLn(__FUNCTION__);
	/*if (!wstrUsername.empty()) {

	}*/
	DebugPrintLn(L"Username from provider: " + (wstrUsername.empty() ? L"empty" : wstrUsername));
	DebugPrintLn(L"Domain from provider: " + (wstrDomainname.empty() ? L"empty" : wstrDomainname));
	if (Configuration::Get().logSensitive) {
		DebugPrintLn(L"Password from provider: " + (wstrPassword.empty() ? L"empty" : wstrPassword));
	}
#endif
	HRESULT hr = S_OK;

	if (!wstrUsername.empty())
	{
		DebugPrintLn("Copying user_name to credential");
		Configuration::Get().credential.user_name = wstrUsername;
	}

	if (!wstrDomainname.empty())
	{
		DebugPrintLn("Copying domain_name to credential");
		Configuration::Get().credential.domain_name = wstrDomainname;
	}

	if (!wstrPassword.empty())
	{
		DebugPrintLn("Copying password to credential");
		Configuration::Get().credential.password = wstrPassword;
	}


	// Copy the field descriptors for each field. This is useful if you want to vary the 
	// field descriptors based on what Usage scenario the credential was created for.
	// Initialize the fields	

	// !!!!!!!!!!!!!!!!!!!!
	// !!!!!!!!!!!!!!!!!!!!
	// TODO: make _rgCredProvFieldDescriptors dynamically allocated depending on current CPUS
	// !!!!!!!!!!!!!!!!!!!!
	// !!!!!!!!!!!!!!!!!!!!

	//for (DWORD i = 0; SUCCEEDED(hr) && i < ARRAYSIZE(_rgCredProvFieldDescriptors); i++)
	for (DWORD i = 0; SUCCEEDED(hr) && i < General::Fields::GetCurrentNumFields(); i++)
	{
		//DebugPrintLn("Copy field #:");
		//DebugPrintLn(i + 1);
		_rgFieldStatePairs[i] = rgfsp[i];
		hr = FieldDescriptorCopy(rgcpfd[i], &_rgCredProvFieldDescriptors[i]);

		if (FAILED(hr))
			break;

		if (s_rgCredProvFieldInitializorsFor[Configuration::Get().provider.usage_scenario] != NULL)
			General::Fields::InitializeField(_rgFieldStrings, s_rgCredProvFieldInitializorsFor[Configuration::Get().provider.usage_scenario][i], i);
	}

	DebugPrintLn("Init result:");
	if (SUCCEEDED(hr))
		DebugPrintLn("OK");
	else
		DebugPrintLn("FAIL");

	return hr;
}

HRESULT CCredential::checkForRealm(std::map<std::string, std::string>& map)
{
	auto& config = Configuration::Get();
	// Check if mapping exists for the current domain
	wstring realm = L"";
	try
	{
		realm = config.realm_map.at(config.credential.domain_name);
	}
	catch (const std::out_of_range & e)
	{
		UNREFERENCED_PARAMETER(e);
		// no mapping - if default domain exists use that
		if (!config.default_realm.empty())
		{
			realm = config.default_realm;
		}
	}

	if (!realm.empty())
	{
		map.try_emplace("realm", Helper::ws2s(realm));
	}

	return S_OK;
}

// LogonUI calls this in order to give us a callback in case we need to notify it of anything.
HRESULT CCredential::Advise(
	__in ICredentialProviderCredentialEvents* pcpce
)
{
	//DebugPrintLn(__FUNCTION__);

	if (_pCredProvCredentialEvents != NULL)
	{
		_pCredProvCredentialEvents->Release();
	}
	_pCredProvCredentialEvents = pcpce;
	_pCredProvCredentialEvents->AddRef();

	/////

	/*if (Configuration::Get().general.startEndpointObserver == true)
	{
		Configuration::Get().general.startEndpointObserver = false;

		if (EndpointObserver::Thread::GetStatus() == EndpointObserver::Thread::STATUS::NOT_RUNNING)
			EndpointObserver::Thread::Create(NULL);
	} */

	return S_OK;
}

// LogonUI calls this to tell us to release the callback.
HRESULT CCredential::UnAdvise()
{
	//DebugPrintLn(__FUNCTION__);

	if (_pCredProvCredentialEvents)
	{
		_pCredProvCredentialEvents->Release();
	}
	_pCredProvCredentialEvents = NULL;
	return S_OK;
}

// Callback for the DialogBox
INT_PTR CALLBACK ChangePasswordProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	auto& config = Configuration::Get();

	wchar_t lpszPassword_old[64];
	wchar_t lpszPassword_new[64];
	wchar_t lpszPassword_new_rep[64];
	WORD cchPassword_new, cchPassword_new_rep, cchPassword_old;

	switch (message)
	{
	case WM_INITDIALOG: {
		DebugPrintLn("Init change password dialog - START");

		// Get the bitmap to display on top of the dialog (same as the logo of the normal tile)
		static HBITMAP hbmp;
		// Check if custom bitmap was set and load that
		std::string szBitmapPath = Helper::ws2s(config.bitmapPath);
		if (!szBitmapPath.empty())
		{
			DWORD dwAttrib = GetFileAttributesA(szBitmapPath.c_str());
			if (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
			{
				hbmp = (HBITMAP)LoadImageA(NULL, szBitmapPath.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
				if (hbmp == NULL)
				{
					DebugPrintLn("Loading custom tile image for dialog failed:");
					DebugPrintLn(GetLastError());
				}
			}
		}
		else {
			// Load the default otherwise
			hbmp = LoadBitmap(HINST_THISDLL, MAKEINTRESOURCE(IDB_TILE_IMAGE));
		}
		// Send the bitmap to the picture control
		SendDlgItemMessage(hDlg, IDC_PICTURE, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hbmp);

		// Languagecode for German(Germany) is 1031
		if (GetUserDefaultUILanguage() == 1031) {
			// Set hints for inputs
			SendDlgItemMessage(hDlg, IDC_EDIT_USERNAME, EM_SETCUEBANNER, (WPARAM)TRUE, (LPARAM)L"Benutzer");
			SendDlgItemMessage(hDlg, IDC_EDIT_OLD_PW, EM_SETCUEBANNER, (WPARAM)TRUE, (LPARAM)L"altes Passwort");
			SendDlgItemMessage(hDlg, IDC_EDIT_NEW_PW, EM_SETCUEBANNER, (WPARAM)TRUE, (LPARAM)L"neues Passwort");
			SendDlgItemMessage(hDlg, IDC_EDIT_NEW_PW_REP, EM_SETCUEBANNER, (WPARAM)TRUE, (LPARAM)L"neues Passwort wiederholen");
			SetWindowText(hDlg, L"Passwort �ndern");
		}
		else {
			// Set hints for inputs
			SendDlgItemMessage(hDlg, IDC_EDIT_USERNAME, EM_SETCUEBANNER, (WPARAM)TRUE, (LPARAM)L"Username");
			SendDlgItemMessage(hDlg, IDC_EDIT_OLD_PW, EM_SETCUEBANNER, (WPARAM)TRUE, (LPARAM)L"Old Password");
			SendDlgItemMessage(hDlg, IDC_EDIT_NEW_PW, EM_SETCUEBANNER, (WPARAM)TRUE, (LPARAM)L"New Password");
			SendDlgItemMessage(hDlg, IDC_EDIT_NEW_PW_REP, EM_SETCUEBANNER, (WPARAM)TRUE, (LPARAM)L"Retype New Password");
			SetWindowText(hDlg, L"Change Password");
		}
		// Set focus to old password edit
		PostMessage(hDlg, WM_SETFOCUS, 0, 0);

		// concat domain\user and put it in the edit control
		std::wstring domainWithUser = config.credential.user_name + L"\\" + config.credential.domain_name;
		//std::wstring tmp_user = std::wstring(Data::Gui::Get()->user_name);
		//std::wstring tmp_domain = std::wstring(Data::Gui::Get()->domain_name);
		//domainWithUser.append(tmp_domain).append(L"\\").append(tmp_user);
		SetDlgItemText(hDlg, IDC_EDIT_USERNAME, domainWithUser.c_str());
		//SetDlgItemText(hDlg, IDC_EDIT_OLD_PW, Data::Gui::Get()->ldap_pass);

		// Set password character to " * "
		SendDlgItemMessage(hDlg, IDC_EDIT_OLD_PW, EM_SETPASSWORDCHAR, (WPARAM)'*', (LPARAM)0);
		SendDlgItemMessage(hDlg, IDC_EDIT_NEW_PW, EM_SETPASSWORDCHAR, (WPARAM)'*', (LPARAM)0);
		SendDlgItemMessage(hDlg, IDC_EDIT_NEW_PW_REP, EM_SETPASSWORDCHAR, (WPARAM)'*', (LPARAM)0);

		// Set the default push button to "Cancel." 
		SendMessage(hDlg, DM_SETDEFID, (WPARAM)IDCANCEL, (LPARAM)0);

		// Center the window
		RECT rc, rcDlg, rcOwner;
		HWND hwndOwner;
		hwndOwner = GetDesktopWindow();

		GetWindowRect(hwndOwner, &rcOwner);
		GetWindowRect(hDlg, &rcDlg);
		CopyRect(&rc, &rcOwner);

		// Offset the owner and dialog box rectangles so that right and bottom values represent the width and height
		// then offset the owner again to discard space taken up by the dialog box. 
		OffsetRect(&rcDlg, -rcDlg.left, -rcDlg.top);
		OffsetRect(&rc, -rc.left, -rc.top);
		OffsetRect(&rc, -rcDlg.right, -rcDlg.bottom);

		// The new position is the sum of half the remaining space and the owner's original position. 
		SetWindowPos(hDlg,
			HWND_TOP,
			rcOwner.left + (rc.right / 2),
			rcOwner.top + (rc.bottom / 2),
			0, 0,          // Ignores size arguments. 
			SWP_NOSIZE);
		DebugPrintLn("Init change password dialog - END");
		return TRUE;
	}
	case WM_COMMAND: {
		// Set the default push button to "OK" when the user enters text. 
		if (HIWORD(wParam) == EN_CHANGE &&
			LOWORD(wParam) == IDC_EDIT_NEW_PW)
		{
			SendMessage(hDlg, DM_SETDEFID, (WPARAM)IDOK, (LPARAM)0);
		}
		switch (wParam)
		{
		case IDOK: {	// User pressed OK - evaluate the inputs
			DebugPrintLn("Evaluate change password dialog - START");
			// Get number of characters for each input
			cchPassword_old = (WORD)SendDlgItemMessage(hDlg, IDC_EDIT_OLD_PW, EM_LINELENGTH, (WPARAM)0, (LPARAM)0);
			cchPassword_new = (WORD)SendDlgItemMessage(hDlg, IDC_EDIT_NEW_PW, EM_LINELENGTH, (WPARAM)0, (LPARAM)0);
			cchPassword_new_rep = (WORD)SendDlgItemMessage(hDlg, IDC_EDIT_NEW_PW_REP, EM_LINELENGTH, (WPARAM)0, (LPARAM)0);

			if (cchPassword_new >= 64 || cchPassword_new_rep >= 64 || cchPassword_old >= 64)
			{
				if (GetUserDefaultUILanguage() == 1031) {
					MessageBox(hDlg, L"Passwort zu lang.", L"Fehler", MB_OK);
				}
				else {
					MessageBox(hDlg, L"Password too long.", L"Error", MB_OK);
				}
				return FALSE;
			}
			else if (cchPassword_new == 0 || cchPassword_new_rep == 0 || cchPassword_old == 0)
			{
				if (GetUserDefaultUILanguage() == 1031) {
					MessageBox(hDlg, L"Bitte f�llen Sie alle Felder aus.", L"Fehler", MB_OK);
				}
				else {
					MessageBox(hDlg, L"Please fill the form entirely.", L"Error", MB_OK);
				}
				return FALSE;
			}

			// Put the number of characters into first word of buffer.
			*((LPWORD)lpszPassword_old) = cchPassword_old;
			*((LPWORD)lpszPassword_new) = cchPassword_new;
			*((LPWORD)lpszPassword_new_rep) = cchPassword_new_rep;

			// Get the characters from line 0 (wparam) into buffer lparam
			SendDlgItemMessage(hDlg, IDC_EDIT_OLD_PW, EM_GETLINE, (WPARAM)0, (LPARAM)lpszPassword_old);
			SendDlgItemMessage(hDlg, IDC_EDIT_NEW_PW, EM_GETLINE, (WPARAM)0, (LPARAM)lpszPassword_new);
			SendDlgItemMessage(hDlg, IDC_EDIT_NEW_PW_REP, EM_GETLINE, (WPARAM)0, (LPARAM)lpszPassword_new_rep);

			// Null-terminate each string. 
			lpszPassword_old[cchPassword_old] = 0;
			lpszPassword_new[cchPassword_new] = 0;
			lpszPassword_new_rep[cchPassword_new_rep] = 0;

			// Compare new passwords
			if (wcscmp(lpszPassword_new, lpszPassword_new_rep) != 0) {
				if (GetUserDefaultUILanguage() == 1031) {
					MessageBox(hDlg, L"Neue Passw�rter stimmen nicht �berein!", L"Fehler", MB_OK);
				}
				else {
					MessageBox(hDlg, L"New Passwords do not match!", L"Error", MB_OK);
				}
				return FALSE;
			}
			// copy new password to password for auto-login
			config.credential.password = wstring(lpszPassword_new);

			/*if (Data::Gui::Get()->ldap_pass || lpszPassword_old)
				wcscpy_s(Data::Gui::Get()->ldap_pass, lpszPassword_old);
			copyNewVals(lpszPassword_new); */

			PWSTR user = const_cast<PWSTR>(config.credential.user_name.c_str());
			PWSTR domain = const_cast<PWSTR>(config.credential.domain_name.c_str());

			// pcpgsr and pcpcs are set in GetSerialization
			HRESULT	hr = General::Logon::KerberosChangePassword(config.provider.pcpgsr, config.provider.pcpcs,
				user, lpszPassword_old, lpszPassword_new, domain);

			if (SUCCEEDED(hr))
				Hook::Serialization::ChangePasswordSuccessfull();
			else
				Hook::Serialization::ChangePasswordFailed();

			// setup for autologin after changing password
			//config.general.bypassEndpoint = true;
			//config.general.bypassDataDeinitialization = true;
			//config.general.bypassDataInitialization = true;
			config.general.bypassEndpoint = true;

			DebugPrintLn("Evaluate CHANGE PASSWORD DIALOG - END");
			EndDialog(hDlg, TRUE);
			return TRUE;
		}
		case IDCANCEL: {
			// Dialog canceled, reset everything
			DebugPrintLn("Exit change password dialog - CANCELED");
			/*	config.credential.passwordMustChange = false;
				config.credential.passwordChanged = false;
				config.general.bypassDataDeinitialization = false;
				config.general.bypassEndpoint = false;
				config.general.bypassDataInitialization = false;*/

			config.credential.passwordMustChange = false;
			config.credential.passwordChanged = false;
			//config.general.bypassDataDeinitialization = false;
			config.general.bypassEndpoint = false;
			//config.general.bypassDataInitialization = false;
			DebugPrintLn("Exit change password dialog - Data::General RESET ");
			EndDialog(hDlg, TRUE);
			Hook::CredentialHooks::ResetScenario(config.provider.pCredProvCredential, config.provider.pCredProvCredentialEvents);
			return TRUE;
		}
		}
		return 0;
	}
	}
	return FALSE;
	UNREFERENCED_PARAMETER(lParam);
}

// LogonUI calls this function when our tile is selected (zoomed).
// If you simply want fields to show/hide based on the selected state,
// there's no need to do anything here - you can set that up in the 
// field definitions.  But if you want to do something
// more complicated, like change the contents of a field when the tile is
// selected, you would do it here.
HRESULT CCredential::SetSelected(__out BOOL* pbAutoLogon)
{
	DebugPrintLn(__FUNCTION__);
	*pbAutoLogon = false;
	HRESULT hr = S_OK;

	auto& config = Configuration::Get();

	if (config.challenge_response.pushAuthenticationSuccessful)
	{
		*pbAutoLogon = true;
	}

	if (config.credential.passwordMustChange && config.provider.usage_scenario == CPUS_UNLOCK_WORKSTATION
		&& config.winVerMajor != 10)
	{
		// We cant handle a password change while the maschine is locked, so we guide the user to sign out and in again like windows does
		DebugPrintLn("Password must change in CPUS_UNLOCK_WORKSTATION");
		_pCredProvCredentialEvents->SetFieldString(this, LUFI_OTP_LARGE_TEXT, L"Go back until you are asked to sign in.");
		_pCredProvCredentialEvents->SetFieldString(this, LUFI_OTP_SMALL_TEXT, L"To change your password sign out and in again.");
		_pCredProvCredentialEvents->SetFieldState(this, LUFI_OTP_LDAP_PASS, CPFS_HIDDEN);
		_pCredProvCredentialEvents->SetFieldState(this, LUFI_OTP_PASS, CPFS_HIDDEN);
	}

	// if passwordMustChange, we want to skip this to get the dialog spawned in GetSerialization
	// if passwordChanged, we want to auto-login
	if (config.credential.passwordMustChange || config.credential.passwordChanged)
	{
		if (config.provider.usage_scenario == CPUS_LOGON || config.winVerMajor == 10)
		{
			*pbAutoLogon = true;
			DebugPrintLn("Password change mode LOGON - AutoLogon true");
		}
		else
		{
			DebugPrintLn("Password change mode UNLOCK - AutoLogon false");
		}
	}

	return hr;
}

// Similarly to SetSelected, LogonUI calls this when your tile was selected
// and now no longer is. The most common thing to do here (which we do below)
// is to clear out the password field.
HRESULT CCredential::SetDeselected()
{
	DebugPrintLn(__FUNCTION__);

	HRESULT hr = S_OK;

	General::Fields::Clear(_rgFieldStrings, _rgCredProvFieldDescriptors, this, _pCredProvCredentialEvents, CLEAR_FIELDS_EDIT_AND_CRYPT);

	//// CONCRETE
	Hook::CredentialHooks::ResetScenario(this, _pCredProvCredentialEvents);
	////

	// Reset password changing in case another user wants to log in
	if (Configuration::Get().credential.passwordChanged)
	{
		Configuration::Get().credential.passwordChanged = false;
	}
	// If its not UNLOCK_WORKSTATION we keep this status to keep the info to sign out first
	if (Configuration::Get().credential.passwordMustChange && (Configuration::Get().provider.usage_scenario != CPUS_UNLOCK_WORKSTATION))
	{
		Configuration::Get().credential.passwordMustChange = false;
	}

	return hr;
}

// Gets info for a particular field of a tile. Called by logonUI to get information to 
// display the tile.
HRESULT CCredential::GetFieldState(
	__in DWORD dwFieldID,
	__out CREDENTIAL_PROVIDER_FIELD_STATE* pcpfs,
	__out CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* pcpfis
)
{
	//DebugPrintLn(__FUNCTION__);

	HRESULT hr;

	// Validate paramters.
	if ((dwFieldID < General::Fields::GetCurrentNumFields()) && pcpfs && pcpfis)
	{
		*pcpfs = _rgFieldStatePairs[dwFieldID].cpfs;
		*pcpfis = _rgFieldStatePairs[dwFieldID].cpfis;

		hr = S_OK;
	}
	else
	{
		hr = E_INVALIDARG;
	}

	//DebugPrintLn(hr);

	return hr;
}

// Sets ppwsz to the string value of the field at the index dwFieldID.
HRESULT CCredential::GetStringValue(
	__in DWORD dwFieldID,
	__deref_out PWSTR* ppwsz
)
{
	//DebugPrintLn(__FUNCTION__);

	HRESULT hr;

	// Check to make sure dwFieldID is a legitimate index.
	if (dwFieldID < General::Fields::GetCurrentNumFields() && ppwsz)
	{
		// Make a copy of the string and return that. The caller
		// is responsible for freeing it.
		hr = SHStrDupW(_rgFieldStrings[dwFieldID], ppwsz);
	}
	else
	{
		hr = E_INVALIDARG;
	}

	//DebugPrintLn(hr);

	return hr;
}

// Gets the image to show in the user tile.
HRESULT CCredential::GetBitmapValue(
	__in DWORD dwFieldID,
	__out HBITMAP* phbmp
)
{
	//DebugPrintLn(__FUNCTION__);

	HRESULT hr = Hook::CredentialHooks::GetBitmapValue(HINST_THISDLL, dwFieldID, phbmp);

	//DebugPrintLn(hr);

	return hr;
}

// Sets pdwAdjacentTo to the index of the field the submit button should be 
// adjacent to. We recommend that the submit button is placed next to the last
// field which the user is required to enter information in. Optional fields
// should be below the submit button.
HRESULT CCredential::GetSubmitButtonValue(
	__in DWORD dwFieldID,
	__out DWORD* pdwAdjacentTo
)
{
	DebugPrintLn(__FUNCTION__);

	HRESULT hr = Hook::CredentialHooks::GetSubmitButtonValue(dwFieldID, pdwAdjacentTo);

	DebugPrintLn(hr);

	return hr;
}

// Sets the value of a field which can accept a string as a value.
// This is called on each keystroke when a user types into an edit field.
HRESULT CCredential::SetStringValue(
	__in DWORD dwFieldID,
	__in PCWSTR pwz
)
{
	HRESULT hr;

	// Validate parameters.
	if (dwFieldID < General::Fields::GetCurrentNumFields() &&
		(CPFT_EDIT_TEXT == _rgCredProvFieldDescriptors[dwFieldID].cpft ||
			CPFT_PASSWORD_TEXT == _rgCredProvFieldDescriptors[dwFieldID].cpft))
	{
		PWSTR* ppwszStored = &_rgFieldStrings[dwFieldID];
		CoTaskMemFree(*ppwszStored);
		hr = SHStrDupW(pwz, ppwszStored);
	}
	else
	{
		hr = E_INVALIDARG;
	}

	//DebugPrintLn(hr);

	return hr;
}

// Returns the number of items to be included in the combobox (pcItems), as well as the 
// currently selected item (pdwSelectedItem).
HRESULT CCredential::GetComboBoxValueCount(
	__in DWORD dwFieldID,
	__out DWORD* pcItems,
	__out_range(< , *pcItems) DWORD* pdwSelectedItem
)
{
	DebugPrintLn(__FUNCTION__);

	HRESULT hr;

	// Validate parameters.
	if (dwFieldID < General::Fields::GetCurrentNumFields() &&
		(CPFT_COMBOBOX == _rgCredProvFieldDescriptors[dwFieldID].cpft))
	{
		hr = Hook::CredentialHooks::GetComboBoxValueCount(dwFieldID, pcItems, pdwSelectedItem);
	}
	else
	{
		hr = E_INVALIDARG;
	}

	DebugPrintLn(hr);

	return S_OK;
}

// Called iteratively to fill the combobox with the string (ppwszItem) at index dwItem.
HRESULT CCredential::GetComboBoxValueAt(
	__in DWORD dwFieldID,
	__in DWORD dwItem,
	__deref_out PWSTR* ppwszItem)
{
	DebugPrintLn(__FUNCTION__);

	HRESULT hr;

	// Validate parameters.
	if (dwFieldID < General::Fields::GetCurrentNumFields() &&
		(CPFT_COMBOBOX == _rgCredProvFieldDescriptors[dwFieldID].cpft))
	{
		hr = Hook::CredentialHooks::GetComboBoxValueAt(dwFieldID, dwItem, ppwszItem);
	}
	else
	{
		hr = E_INVALIDARG;
	}

	DebugPrintLn(hr);

	return hr;
}

// Called when the user changes the selected item in the combobox.
HRESULT CCredential::SetComboBoxSelectedValue(
	__in DWORD dwFieldID,
	__in DWORD dwSelectedItem
)
{
	DebugPrintLn(__FUNCTION__);

	HRESULT hr = 0;

	// Validate parameters.
	if (dwFieldID < General::Fields::GetCurrentNumFields() &&
		(CPFT_COMBOBOX == _rgCredProvFieldDescriptors[dwFieldID].cpft))
	{
		hr = Hook::CredentialHooks::SetComboBoxSelectedValue(this, _pCredProvCredentialEvents, dwFieldID, dwSelectedItem, _dwComboIndex);
	}
	else
	{
		hr = E_INVALIDARG;
	}

	DebugPrintLn(hr);

	return hr;
}

HRESULT CCredential::GetCheckboxValue(
	__in DWORD dwFieldID,
	__out BOOL* pbChecked,
	__deref_out PWSTR* ppwszLabel
)
{
	// Called to check the initial state of the checkbox
	DebugPrintLn(__FUNCTION__);

	*pbChecked = FALSE;
	SHStrDupW(L"Use offline token.", ppwszLabel); // TODO custom text?

	return S_OK;
}

HRESULT CCredential::SetCheckboxValue(
	__in DWORD dwFieldID,
	__in BOOL bChecked
)
{
	DebugPrintLn(__FUNCTION__);

	Configuration::Get().use_offline = bChecked;
	return S_OK;
}

//------------- 
// The following methods are for logonUI to get the values of various UI elements and then communicate
// to the credential about what the user did in that field.  However, these methods are not implemented
// because our tile doesn't contain these types of UI elements
HRESULT CCredential::CommandLinkClicked(__in DWORD dwFieldID)
{
	UNREFERENCED_PARAMETER(dwFieldID);
	DebugPrintLn("COMMAND LINK CLICKED!");
	return S_OK;
}

//------ end of methods for controls we don't have in our tile ----//

// Collect the username and password into a serialized credential for the correct usage scenario 
// (logon/unlock is what's demonstrated in this sample).  LogonUI then passes these credentials 
// back to the system to log on.
HRESULT CCredential::GetSerialization(
	__out CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* pcpgsr,
	__out CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs,
	__deref_out_opt PWSTR* ppwszOptionalStatusText,
	__out CREDENTIAL_PROVIDER_STATUS_ICON* pcpsiOptionalStatusIcon
)
{
	DebugPrintLn(__FUNCTION__);

	*pcpgsr = CPGSR_RETURN_NO_CREDENTIAL_FINISHED;

	HRESULT hr = E_FAIL, retVal = S_OK;

	auto& config = Configuration::Get();

	/*
	CPGSR_NO_CREDENTIAL_NOT_FINISHED
	No credential was serialized because more information is needed.

	CPGSR_NO_CREDENTIAL_FINISHED
	This serialization response means that the Credential Provider has not serialized a credential but it has completed its work. This response has multiple meanings.
	It can mean that no credential was serialized and the user should not try again. This response can also mean no credential was submitted but the credential�s work is complete.
	For instance, in the Change Password scenario, this response implies success.

	CPGSR_RETURN_CREDENTIAL_FINISHED
	A credential was serialized. This response implies a serialization structure was passed back.

	CPGSR_RETURN_NO_CREDENTIAL_FINISHED
	The credential provider has not serialized a credential, but has completed its work. The difference between this value and CPGSR_NO_CREDENTIAL_FINISHED is that this flag
	will force the logon UI to return, which will unadvise all the credential providers.
	*/

	// reference parameters to internal datastructures (we need them in the hooks)
	//HOOK_CHECK_CRITICAL(Hook::Serialization::Initialization(), CleanUpAndReturn);
	//Hook::Serialization::Initialization();
	config.provider.pCredProvCredentialEvents = _pCredProvCredentialEvents;
	config.provider.pCredProvCredential = this;

	config.provider.pcpcs = pcpcs;
	config.provider.pcpgsr = pcpgsr;

	config.provider.status_icon = pcpsiOptionalStatusIcon;
	config.provider.status_text = ppwszOptionalStatusText;

	config.provider.field_strings = _rgFieldStrings;
	config.provider.num_field_strings = General::Fields::GetCurrentNumFields();


	PWSTR currentUsername = const_cast<PWSTR>(config.credential.user_name.c_str());
	PWSTR currentPassword = const_cast<PWSTR>(config.credential.password.c_str());
	PWSTR currentDomain = const_cast<PWSTR>(config.credential.domain_name.c_str());

	// open dialog for old/new password
	if (config.credential.passwordMustChange)
	{
		HWND hwndOwner = nullptr;
		HRESULT res = E_FAIL;
		if (_pCredProvCredentialEvents)
		{
			res = _pCredProvCredentialEvents->OnCreatingWindow(&hwndOwner); // get a handle to the owner window
		}
		if (SUCCEEDED(res))
		{
			if (config.provider.usage_scenario == CPUS_LOGON || config.winVerMajor == 10)
			{//It's password change on Logon we can handle that
				DebugPrintLn("Passwordchange with CPUS_LOGON - open Dialog");
				::DialogBox(HINST_THISDLL,					// application instance
					MAKEINTRESOURCE(IDD_DIALOG1),			// dialog box resource
					hwndOwner,								// owner window
					ChangePasswordProc						// dialog box window procedure
				);
				//goto CleanUpAndReturn;
			}
		}
		else
		{
			DbgRelPrintLn("Opening password change dialog failed: Handle to owner window is missing");
		}
	}

	if (config.credential.passwordChanged)
	{
		DebugPrintLn("Password change success- Set Data::General for autologon");
		config.general.bypassEndpoint = true;
		//config.general.bypassDataDeinitialization = true;
		//config.general.bypassDataInitialization = true;
	}

	if (config.endpoint.status == ENDPOINT_STATUS_NOT_SET && config.general.bypassEndpoint == false)
	{
		// Something went wrong

	}

	if (config.endpoint.userCanceled)
	{
		//Hook::Serialization::EndpointCallCancelled();
		*config.provider.status_icon = CPSI_ERROR;
		*config.provider.pcpgsr = CPGSR_NO_CREDENTIAL_FINISHED;
		SHStrDupW(L"Logon cancelled", config.provider.status_text);
		return S_FALSE;
	}

	if (config.endpoint.status == ENDPOINT_STATUS_AUTH_CONTINUE && config.challenge_response.pushAuthenticationSuccessful == false)
	{
		// Prepare for the second step (input only OTP)
		Configuration::Get().general.clearFields = false;
		General::Fields::SetScenario(config.provider.pCredProvCredential, config.provider.pCredProvCredentialEvents,
			General::Fields::SCENARIO_SECOND_STEP, NULL, L"Please enter your second factor:");
		*config.provider.pcpgsr = CPGSR_NO_CREDENTIAL_NOT_FINISHED;

		if (config.challenge_response.usingPushToken)
		{
			//_pCredProvCredentialEvents->SetFieldState(config.provider.pCredProvCredential, LPFI_PUSH_COMMAND_LINK, CPFS_DISPLAY_IN_SELECTED_TILE);
			// If push token is used, start polling
			//future<HRESULT> futurePollingResult = std::async(_endpoint.pollForTransaction, config.challenge_response.transactionID);
			/*if (SUCCEEDED(res))
			{
				// If successful, skip to login
				config.endpoint.status = ENDPOINT_STATUS_AUTH_OK;
				//config.general.bypassEndpoint = true;
			} */
		}
	}
	else if (config.endpoint.status == ENDPOINT_STATUS_AUTH_OK || config.general.bypassEndpoint || config.challenge_response.pushAuthenticationSuccessful)
	{
		// LOG IN
		if (config.endpoint.status == ENDPOINT_STATUS_AUTH_OK)
		{
			// If windows password is wrong, treat it as new logon
			config.isSecondStep = false;
			config.endpoint.status = ENDPOINT_STATUS_NOT_SET;
		}

		if (config.provider.usage_scenario == CPUS_CREDUI)
		{
			hr = General::Logon::CredPackAuthentication(pcpgsr, pcpcs, config.provider.usage_scenario, currentUsername,
				currentPassword, currentDomain);
		}
		else
		{
			hr = General::Logon::KerberosLogon(pcpgsr, pcpcs, config.provider.usage_scenario, currentUsername,
				currentPassword, currentDomain);
		}
		if (SUCCEEDED(hr))
		{
			if (config.credential.passwordChanged) { config.credential.passwordChanged = false; }
			//HOOK_CHECK_CRITICAL(Hook::Serialization::KerberosCallSuccessfull(), CleanUpAndReturn);
		}
		else
		{
			//HOOK_CHECK_CRITICAL(Hook::Serialization::KerberosCallFailed(), CleanUpAndReturn);
			retVal = S_FALSE;
		}
	}
	else if (config.endpoint.status == ENDPOINT_STATUS_AUTH_FAIL)
	{
		wstring otpFailureText = config.otpFailureText.empty() ? L"Wrong One-Time-Password!" : config.otpFailureText;
		SHStrDupW(otpFailureText.c_str(), config.provider.status_text);
		*config.provider.status_icon = CPSI_ERROR;
		*config.provider.pcpgsr = CPGSR_NO_CREDENTIAL_NOT_FINISHED;
	}
	else
	{
		//HOOK_CHECK_CRITICAL(Hook::Serialization::EndpointCallFailed(), CleanUpAndReturn);
		Hook::Serialization::EndpointCallFailed();
		// Jump to the first login window
		Hook::CredentialHooks::ResetScenario(this, _pCredProvCredentialEvents);
		retVal = S_FALSE;
	}

	if (config.general.clearFields)
	{
		General::Fields::Clear(_rgFieldStrings, _rgCredProvFieldDescriptors, this, _pCredProvCredentialEvents, CLEAR_FIELDS_CRYPT);
	}
	else
	{
		config.general.clearFields = true; // it's a one-timer...
	}

	// Reset endpoint status 
	config.endpoint.status = ENDPOINT_STATUS_NOT_SET;

	////////////// DEBUG
	if (pcpgsr)
	{
		if (*pcpgsr == CPGSR_NO_CREDENTIAL_FINISHED) { DebugPrintLn("CPGSR_NO_CREDENTIAL_FINISHED"); }
		if (*pcpgsr == CPGSR_NO_CREDENTIAL_NOT_FINISHED) { DebugPrintLn("CPGSR_NO_CREDENTIAL_NOT_FINISHED"); }
		if (*pcpgsr == CPGSR_RETURN_CREDENTIAL_FINISHED) { DebugPrintLn("CPGSR_RETURN_CREDENTIAL_FINISHED"); }
		if (*pcpgsr == CPGSR_RETURN_NO_CREDENTIAL_FINISHED) { DebugPrintLn("CPGSR_RETURN_NO_CREDENTIAL_FINISHED"); }
	}
	else { DebugPrintLn("pcpgsr is a nullpointer!"); }
	DebugPrintLn("CCredential::GetSerialization - END");
	///////////////////////////////////
	return retVal;
}

// Connect is called first after the submit button is pressed.
HRESULT CCredential::Connect(__in IQueryContinueWithStatus* pqcws)
{
	DebugPrintLn(__FUNCTION__);

	auto& config = Configuration::Get();
	if (!config.realm_map.empty())
	{
		DebugPrintLn("Realm Map:");
		for (auto& entry : config.realm_map)
		{
			DebugPrintLn(entry.first + L"=" + entry.second);
		}
	}

	config.endpoint.pQueryContinueWithStatus = pqcws;

	config.provider.pCredProvCredential = this;
	config.provider.pCredProvCredentialEvents = _pCredProvCredentialEvents;
	config.provider.field_strings = _rgFieldStrings;
	config.provider.num_field_strings = General::Fields::GetCurrentNumFields();

	Hook::Serialization::ReadFieldValues();

	if (config.general.bypassEndpoint == false || config.challenge_response.pushAuthenticationSuccessful == true)
	{
		// TODO: CALL ENDPOINT - ENDPOINT STATUS DEICIDES HOW TO CONTINUE (2STEP ETC), remove sending password/empty
		if (config.twoStepHideOTP && !config.isSecondStep)
		{
			if (!config.twoStepSendEmptyPassword && !config.twoStepSendPassword)
			{
				// Delay for a short moment, otherwise logonui crashes (???)
				this_thread::sleep_for(chrono::milliseconds(200));
				// Then skip to next step
				config.isSecondStep = true;
				config.endpoint.status = ENDPOINT_STATUS_AUTH_CONTINUE;
			}
			else if (config.twoStepSendEmptyPassword && !config.twoStepSendPassword)
			{
				// Send an empty pass in the FIRST step
				map<string, string> params;
				params.try_emplace("user", Helper::ws2s(config.credential.user_name));
				params.try_emplace("pass", "");
				checkForRealm(params);
				string response = _endpoint.connect(PI_ENDPOINT_VALIDATE_CHECK, params, RequestMethod::POST);
				config.isSecondStep = true;
				config.endpoint.status = ENDPOINT_STATUS_AUTH_CONTINUE;
			}
			else if (!config.twoStepSendEmptyPassword && config.twoStepSendPassword)
			{
				// Send the windows password in the first step, which may trigger challenges
				map<string, string> params;
				params.try_emplace("user", Helper::ws2s(config.credential.user_name));
				params.try_emplace("pass", Helper::ws2s(config.credential.password));
				checkForRealm(params);

				string response = _endpoint.connect(PI_ENDPOINT_VALIDATE_CHECK, params, RequestMethod::POST);
				_endpoint.parseTriggerRequest(response);

				config.isSecondStep = true;
				config.endpoint.status = ENDPOINT_STATUS_AUTH_CONTINUE;

				// if both pushtoken and classic OTP are available, start the polling in background
				if (config.challenge_response.tta == TTA::BOTH)
				{
					_pollResult = std::async(std::launch::async, &Endpoint::pollForTransactionWithLoop, &_endpoint, config.challenge_response.transactionID);
					//_pollResult = std::async(std::launch::async, poll, config.challenge_response.transactionID);
				}
				// if only push is available, start polling in main thread
				// TODO cancel button??
				else if (config.challenge_response.tta == TTA::PUSH)
				{
					HRESULT res = E_FAIL;
					pqcws->SetStatusMessage(L"Please confirm the authentication on your mobile device!");
					while (res != ENDPOINT_STATUS_AUTH_OK)
					{
						res = _endpoint.pollForTransactionSingle(config.challenge_response.transactionID);
					}
					// When polling returns as successful, the authentication must be finialized
					res = _endpoint.finalizePolling(Helper::ws2s(config.credential.user_name), config.challenge_response.transactionID);
					if (res == ENDPOINT_STATUS_AUTH_OK) {
						config.endpoint.status = ENDPOINT_STATUS_AUTH_OK;
						config.challenge_response.pushAuthenticationSuccessful = true;
					}
				}
				else
				{
					// Only classic OTP available, do nothing else in the first step
				}
			}
			else
			{
				DebugPrintLn("UNKNOWN STATE:");
				config.isSecondStep;
			}
		}
		//////////////////// SECOND STEP ////////////////////////
		else if (config.twoStepHideOTP && config.isSecondStep)
		{
			//if (config.challenge_response.tta == TTA::BOTH || config.challenge_response.tta == TTA::PUSH)
			//{

			//}
			//else
			//{ 
			// Send username and OTP in SECOND step
			map<string, string> params;
			params.try_emplace("user", Helper::ws2s(config.credential.user_name));
			params.try_emplace("pass", Helper::ws2s(config.credential.otp));
			checkForRealm(params);

			string response = _endpoint.connect(PI_ENDPOINT_VALIDATE_CHECK, params, RequestMethod::POST);
			config.endpoint.status = _endpoint.parseAuthenticationRequest(response);
			//}

		}
		//////// NORMAL SETUP WITH 3 FIELDS -> SEND OTP ////////
		else
		{
			map<string, string> params;
			params.try_emplace("user", Helper::ws2s(config.credential.user_name));
			params.try_emplace("pass", Helper::ws2s(config.credential.otp));
			checkForRealm(params);

			string response = _endpoint.connect(PI_ENDPOINT_VALIDATE_CHECK, params, RequestMethod::POST);
			config.endpoint.status = _endpoint.parseAuthenticationRequest(response);
		}
	}
	config.endpoint.pQueryContinueWithStatus = nullptr;
	DebugPrintLn("Connect - END");
	return S_OK; // always S_OK
}

HRESULT CCredential::Disconnect()
{
	return E_NOTIMPL;
}

// ReportResult is completely optional.  Its purpose is to allow a credential to customize the string
// and the icon displayed in the case of a logon failure.  For example, we have chosen to 
// customize the error shown in the case of bad username/password and in the case of the account
// being disabled.
HRESULT CCredential::ReportResult(
	__in NTSTATUS ntsStatus,
	__in NTSTATUS ntsSubstatus,
	__deref_out_opt PWSTR* ppwszOptionalStatusText,
	__out CREDENTIAL_PROVIDER_STATUS_ICON* pcpsiOptionalStatusIcon
)
{
#ifdef _DEBUG
	DebugPrintLn(__FUNCTION__);
	DebugPrintLn("ntsStatus:");
	DebugPrintLn(ntsStatus);
	DebugPrintLn("ntsSubstatus:");
	DebugPrintLn(ntsSubstatus);
#endif

	UNREFERENCED_PARAMETER(ppwszOptionalStatusText);
	UNREFERENCED_PARAMETER(pcpsiOptionalStatusIcon);

	Configuration::Get().credential.passwordMustChange = (ntsStatus == STATUS_PASSWORD_MUST_CHANGE) || (ntsSubstatus == STATUS_PASSWORD_EXPIRED);

	if (Configuration::Get().credential.passwordMustChange)
	{
		DebugPrintLn("Status: Password must change");
		return E_NOTIMPL;
	}

	// check if the password update was NOT successfull
	// these two are for new passwords not conform to password policies
	bool pwNotUpdated = (ntsStatus == STATUS_PASSWORD_RESTRICTION) || (ntsSubstatus == STATUS_ILL_FORMED_PASSWORD);
	if (pwNotUpdated)
	{
		DebugPrintLn("Status: Password update failed: Not conform to policies");
	}
	// this catches the wrong old password, 
	pwNotUpdated = pwNotUpdated || ((ntsStatus == STATUS_LOGON_FAILURE) && (ntsSubstatus == STATUS_INTERNAL_ERROR));

	if (pwNotUpdated)
	{
		// it wasn't updated so we start over again
		Configuration::Get().credential.passwordMustChange = true;
		Configuration::Get().credential.passwordChanged = false;
	}

	if (ntsStatus == STATUS_LOGON_FAILURE && !pwNotUpdated)
	{
		Hook::CredentialHooks::ResetScenario(this, _pCredProvCredentialEvents);
	}
	return E_NOTIMPL;
}


