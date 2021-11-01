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

#include "helpers.h"
#include <intsafe.h>
#include <wincred.h>
#include <string>

// 
// Copies the field descriptor pointed to by rcpfd into a buffer allocated 
// using CoTaskMemAlloc. Returns that buffer in ppcpfd.
// 
HRESULT FieldDescriptorCoAllocCopy(
	__in const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR& rcpfd,
	__deref_out CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** ppcpfd
)
{
	HRESULT hr;
	DWORD cbStruct = sizeof(**ppcpfd);

	CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR* pcpfd = (CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR*)CoTaskMemAlloc(cbStruct);
	if (pcpfd)
	{
		pcpfd->dwFieldID = rcpfd.dwFieldID;
		pcpfd->cpft = rcpfd.cpft;

		if (rcpfd.pszLabel)
		{
			hr = SHStrDupW(rcpfd.pszLabel, &pcpfd->pszLabel);
		}
		else
		{
			pcpfd->pszLabel = nullptr;
			hr = S_OK;
		}
	}
	else
	{
		hr = E_OUTOFMEMORY;
	}

	if (SUCCEEDED(hr))
	{
		*ppcpfd = pcpfd;
	}
	else
	{
		CoTaskMemFree(pcpfd);
		*ppcpfd = nullptr;
	}

	return hr;
}

//
// Copies rcpfd into the buffer pointed to by pcpfd. The caller is responsible for
// allocating pcpfd. This function uses CoTaskMemAlloc to allocate memory for 
// pcpfd->pszLabel.
//
HRESULT FieldDescriptorCopy(
	__in const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR& rcpfd,
	__deref_out CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR* pcpfd
)
{

	HRESULT hr = E_FAIL;
	CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR cpfd;

	cpfd.dwFieldID = rcpfd.dwFieldID;
	cpfd.cpft = rcpfd.cpft;

	if (rcpfd.pszLabel)
	{
		hr = SHStrDupW(rcpfd.pszLabel, &cpfd.pszLabel);
	}
	else
	{
		cpfd.pszLabel = nullptr;
		hr = S_OK;
	}

	if (SUCCEEDED(hr))
	{
		*pcpfd = cpfd;
	}

	return hr;
}

//
// This function copies the length of pwz and the pointer pwz into the UNICODE_STRING structure
// This function is intended for serializing a credential in GetSerialization only.
// Note that this function just makes a copy of the string pointer. It DOES NOT ALLOCATE storage!
// Be very, very sure that this is what you want, because it probably isn't outside of the
// exact GetSerialization call where the sample uses it.
//
HRESULT UnicodeStringInitWithString(
	__in PWSTR pwz,
	__deref_out UNICODE_STRING* pus
)
{
	HRESULT hr = E_FAIL;
	if (pwz)
	{
		const size_t lenString = lstrlen(pwz);
		USHORT usCharCount;
		hr = SizeTToUShort(lenString, &usCharCount);
		if (SUCCEEDED(hr))
		{
			USHORT usSize;
			hr = SizeTToUShort(sizeof(WCHAR), &usSize);
			if (SUCCEEDED(hr))
			{
				hr = UShortMult(usCharCount, usSize, &(pus->Length)); // Explicitly NOT including NULL terminator
				if (SUCCEEDED(hr))
				{
					pus->MaximumLength = pus->Length;
					pus->Buffer = pwz;
					hr = S_OK;
				}
				else
				{
					hr = HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
				}
			}
		}
	}
	else
	{
		hr = E_INVALIDARG;
	}
	return hr;
}

//
// The following function is intended to be used ONLY with the Kerb*Pack functions.  It does
// no bounds-checking because its callers have precise requirements and are written to respect 
// its limitations.
// You can read more about the UNICODE_STRING type at:
// http://msdn.microsoft.com/library/default.asp?url=/library/en-us/secauthn/security/unicode_string.asp
//
static void _UnicodeStringPackedUnicodeStringCopy(
	__in const UNICODE_STRING& rus,
	__in PWSTR pwzBuffer,
	__out UNICODE_STRING* pus
)
{
	pus->Length = rus.Length;
	pus->MaximumLength = rus.Length;
	pus->Buffer = pwzBuffer;

	CopyMemory(pus->Buffer, rus.Buffer, pus->Length);
}

//
// Initialize the members of a KERB_INTERACTIVE_UNLOCK_LOGON with weak references to the
// passed-in strings.  This is useful if you will later use KerbInteractiveUnlockLogonPack
// to serialize the structure.  
//
// The password is stored in encrypted form for CPUS_LOGON and CPUS_UNLOCK_WORKSTATION
// because the system can accept encrypted credentials.  It is not encrypted in CPUS_CREDUI
// because we cannot know whether our caller can accept encrypted credentials.
//
HRESULT KerbInteractiveUnlockLogonInit(
	__in PWSTR pwzDomain,
	__in PWSTR pwzUsername,
	__in PWSTR pwzPassword,
	__in CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
	__out KERB_INTERACTIVE_UNLOCK_LOGON* pkiul
)
{
	KERB_INTERACTIVE_UNLOCK_LOGON kiul;
	ZeroMemory(&kiul, sizeof(kiul));

	KERB_INTERACTIVE_LOGON* pkil = &kiul.Logon;

	// Note: this method uses custom logic to pack a KERB_INTERACTIVE_UNLOCK_LOGON with a
	// serialized credential.  We could replace the calls to UnicodeStringInitWithString
	// and KerbInteractiveUnlockLogonPack with a single call to CredPackAuthenticationBuffer,
	// but that API has a drawback: it returns a KERB_INTERACTIVE_UNLOCK_LOGON whose
	// MessageType is always KerbInteractiveLogon.  
	//
	// If we only handled CPUS_LOGON, this drawback would not be a problem. For 
	// CPUS_UNLOCK_WORKSTATION, we could cast the output buffer of CredPackAuthenticationBuffer
	// to KERB_INTERACTIVE_UNLOCK_LOGON and modify the MessageType to KerbWorkstationUnlockLogon,
	// but such a cast would be unsupported -- the output format of CredPackAuthenticationBuffer
	// is not officially documented.

	// Initialize the UNICODE_STRINGS to share our username and password strings.
	HRESULT hr = UnicodeStringInitWithString(pwzDomain, &pkil->LogonDomainName);
	if (SUCCEEDED(hr))
	{
		hr = UnicodeStringInitWithString(pwzUsername, &pkil->UserName);
		if (SUCCEEDED(hr))
		{
			hr = UnicodeStringInitWithString(pwzPassword, &pkil->Password);
			if (SUCCEEDED(hr))
			{
				// Set a MessageType based on the usage scenario.
				switch (cpus)
				{
				case CPUS_UNLOCK_WORKSTATION:
					pkil->MessageType = KerbWorkstationUnlockLogon;
					hr = S_OK;
					break;

				case CPUS_LOGON:
					pkil->MessageType = KerbInteractiveLogon;
					hr = S_OK;
					break;

				case CPUS_CREDUI:
					pkil->MessageType = (KERB_LOGON_SUBMIT_TYPE)0; // MessageType does not apply to CredUI
					hr = S_OK;
					break;

				default:
					hr = E_FAIL;
					break;
				}

				if (SUCCEEDED(hr))
				{
					// KERB_INTERACTIVE_UNLOCK_LOGON is just a series of structures.  A
					// flat copy will properly initialize the output parameter.
					CopyMemory(pkiul, &kiul, sizeof(*pkiul));
				}
			}
		}
	}

	return hr;
}

//
// WinLogon and LSA consume "packed" KERB_INTERACTIVE_UNLOCK_LOGONs.  In these, the PWSTR members of each
// UNICODE_STRING are not actually pointers but byte offsets into the overall buffer represented
// by the packed KERB_INTERACTIVE_UNLOCK_LOGON.  For example:
// 
// rkiulIn.Logon.LogonDomainName.Length = 14                                    -> Length is in bytes, not characters
// rkiulIn.Logon.LogonDomainName.Buffer = sizeof(KERB_INTERACTIVE_UNLOCK_LOGON) -> LogonDomainName begins immediately
//                                                                              after the KERB_... struct in the buffer
// rkiulIn.Logon.UserName.Length = 10
// rkiulIn.Logon.UserName.Buffer = sizeof(KERB_INTERACTIVE_UNLOCK_LOGON) + 14   -> UNICODE_STRINGS are NOT null-terminated
//
// rkiulIn.Logon.Password.Length = 16
// rkiulIn.Logon.Password.Buffer = sizeof(KERB_INTERACTIVE_UNLOCK_LOGON) + 14 + 10
// 
// THere's more information on this at:
// http://msdn.microsoft.com/msdnmag/issues/05/06/SecurityBriefs/#void
//

HRESULT KerbInteractiveUnlockLogonPack(
	__in const KERB_INTERACTIVE_UNLOCK_LOGON& rkiulIn,
	__deref_out_bcount(*pcb) BYTE** prgb,
	__out DWORD* pcb
)
{
	HRESULT hr = E_FAIL;

	const KERB_INTERACTIVE_LOGON* pkilIn = &rkiulIn.Logon;

	// alloc space for struct plus extra for the three strings
	DWORD cb = sizeof(rkiulIn) +
		pkilIn->LogonDomainName.Length +
		pkilIn->UserName.Length +
		pkilIn->Password.Length;

	KERB_INTERACTIVE_UNLOCK_LOGON* pkiulOut = (KERB_INTERACTIVE_UNLOCK_LOGON*)CoTaskMemAlloc(cb);
	if (pkiulOut)
	{
		ZeroMemory(&pkiulOut->LogonId, sizeof(pkiulOut->LogonId));

		//
		// point pbBuffer at the beginning of the extra space
		//
		BYTE* pbBuffer = (BYTE*)pkiulOut + sizeof(*pkiulOut);

		//
		// set up the Logon structure within the KERB_INTERACTIVE_UNLOCK_LOGON
		//
		KERB_INTERACTIVE_LOGON* pkilOut = &pkiulOut->Logon;

		pkilOut->MessageType = pkilIn->MessageType;

		//
		// copy each string,
		// fix up appropriate buffer pointer to be offset,
		// advance buffer pointer over copied characters in extra space
		//
		_UnicodeStringPackedUnicodeStringCopy(pkilIn->LogonDomainName, (PWSTR)pbBuffer, &pkilOut->LogonDomainName);
		pkilOut->LogonDomainName.Buffer = (PWSTR)(pbBuffer - (BYTE*)pkiulOut);
		pbBuffer += pkilOut->LogonDomainName.Length;

		_UnicodeStringPackedUnicodeStringCopy(pkilIn->UserName, (PWSTR)pbBuffer, &pkilOut->UserName);
		pkilOut->UserName.Buffer = (PWSTR)(pbBuffer - (BYTE*)pkiulOut);
		pbBuffer += pkilOut->UserName.Length;

		_UnicodeStringPackedUnicodeStringCopy(pkilIn->Password, (PWSTR)pbBuffer, &pkilOut->Password);
		pkilOut->Password.Buffer = (PWSTR)(pbBuffer - (BYTE*)pkiulOut);

		*prgb = (BYTE*)pkiulOut;
		*pcb = cb;

		hr = S_OK;
	}
	else
	{
		hr = E_OUTOFMEMORY;
	}

	return hr;
}

HRESULT KerbChangePasswordPack(
	const KERB_CHANGEPASSWORD_REQUEST& rkcrIn,
	BYTE** prgb,
	DWORD* pcb
)
{
	HRESULT hr = E_FAIL;

	DWORD cb = sizeof(rkcrIn) +
		rkcrIn.DomainName.Length +
		rkcrIn.AccountName.Length +
		rkcrIn.OldPassword.Length +
		rkcrIn.NewPassword.Length;

	KERB_CHANGEPASSWORD_REQUEST* pkcr = (KERB_CHANGEPASSWORD_REQUEST*)CoTaskMemAlloc(cb);

	if (pkcr)
	{
		pkcr->MessageType = rkcrIn.MessageType;

		BYTE* pbBuffer = (BYTE*)pkcr + sizeof(KERB_CHANGEPASSWORD_REQUEST);

		_UnicodeStringPackedUnicodeStringCopy(rkcrIn.DomainName, (PWSTR)pbBuffer, &pkcr->DomainName);
		pkcr->DomainName.Buffer = (PWSTR)(pbBuffer - (BYTE*)pkcr);
		pbBuffer += pkcr->DomainName.Length;

		_UnicodeStringPackedUnicodeStringCopy(rkcrIn.AccountName, (PWSTR)pbBuffer, &pkcr->AccountName);
		pkcr->AccountName.Buffer = (PWSTR)(pbBuffer - (BYTE*)pkcr);
		pbBuffer += pkcr->AccountName.Length;

		_UnicodeStringPackedUnicodeStringCopy(rkcrIn.OldPassword, (PWSTR)pbBuffer, &pkcr->OldPassword);
		pkcr->OldPassword.Buffer = (PWSTR)(pbBuffer - (BYTE*)pkcr);
		pbBuffer += pkcr->OldPassword.Length;

		_UnicodeStringPackedUnicodeStringCopy(rkcrIn.NewPassword, (PWSTR)pbBuffer, &pkcr->NewPassword);
		pkcr->NewPassword.Buffer = (PWSTR)(pbBuffer - (BYTE*)pkcr);

		*prgb = (BYTE*)pkcr;
		*pcb = cb;

		hr = S_OK;
	}
	else
	{
		hr = E_OUTOFMEMORY;
	}
	return hr;
}

// 
// This function packs the string pszSourceString in pszDestinationString
// for use with LSA functions including LsaLookupAuthenticationPackage.
//
static HRESULT _LsaInitString(
	__out PSTRING pszDestinationString,
	__in PCSTR pszSourceString
)
{
	const size_t cchLength = lstrlenA(pszSourceString);
	USHORT usLength;
	HRESULT hr = SizeTToUShort(cchLength, &usLength);
	if (SUCCEEDED(hr))
	{
		pszDestinationString->Buffer = (PCHAR)pszSourceString;
		pszDestinationString->Length = usLength;
		pszDestinationString->MaximumLength = pszDestinationString->Length + 1;
		hr = S_OK;
	}
	return hr;
}

//
// Retrieves the 'negotiate' AuthPackage from the LSA. In this case, Kerberos
// For more information on auth packages see this msdn page:
// http://msdn.microsoft.com/library/default.asp?url=/library/en-us/secauthn/security/msv1_0_lm20_logon.asp
//
HRESULT RetrieveNegotiateAuthPackage(__out ULONG* pulAuthPackage)
{
	HRESULT hr;
	HANDLE hLsa;

	NTSTATUS status = LsaConnectUntrusted(&hLsa);
	if (SUCCEEDED(HRESULT_FROM_NT(status)))
	{
		ULONG ulAuthPackage;
		LSA_STRING lsaszKerberosName;
		_LsaInitString(&lsaszKerberosName, NEGOSSP_NAME_A);

		status = LsaLookupAuthenticationPackage(hLsa, &lsaszKerberosName, &ulAuthPackage);
		if (SUCCEEDED(HRESULT_FROM_NT(status)))
		{
			*pulAuthPackage = ulAuthPackage;
			hr = S_OK;
		}
		else
		{
			hr = HRESULT_FROM_NT(status);
		}
		LsaDeregisterLogonProcess(hLsa);
	}
	else
	{
		hr = HRESULT_FROM_NT(status);
	}

	return hr;
}

//
// Return a copy of pwzToProtect encrypted with the CredProtect API.
//
// pwzToProtect must not be NULL or the empty string.
//
static HRESULT _ProtectAndCopyString(
	__in PCWSTR pwzToProtect,
	__deref_out PWSTR* ppwzProtected
)
{
	*ppwzProtected = nullptr;

	// pwzToProtect is const, but CredProtect takes a non-const string.
	// So, make a copy that we know isn't const.
	PWSTR pwzToProtectCopy;
	HRESULT hr = SHStrDupW(pwzToProtect, &pwzToProtectCopy);
	if (SUCCEEDED(hr))
	{
		// The first call to CredProtect determines the length of the encrypted string.
		// Because we pass a NULL output buffer, we expect the call to fail.
		//
		// Note that the third parameter to CredProtect, the number of characters of pwzToProtectCopy
		// to encrypt, must include the NULL terminator!
		DWORD cchProtected = 0;
		if (!CredProtectW(FALSE, pwzToProtectCopy, (DWORD)wcslen(pwzToProtectCopy) + 1, NULL, &cchProtected, NULL))
		{
			DWORD dwErr = GetLastError();

			if ((ERROR_INSUFFICIENT_BUFFER == dwErr) && (0 < cchProtected))
			{
				// Allocate a buffer long enough for the encrypted string.
				PWSTR pwzProtected = (PWSTR)CoTaskMemAlloc(cchProtected * sizeof(WCHAR));
				if (pwzProtected)
				{
					// The second call to CredProtect actually encrypts the string.
					if (CredProtectW(FALSE, pwzToProtectCopy, (DWORD)wcslen(pwzToProtectCopy) + 1, pwzProtected, &cchProtected, NULL))
					{
						*ppwzProtected = pwzProtected;
						hr = S_OK;
					}
					else
					{
						CoTaskMemFree(pwzProtected);

						dwErr = GetLastError();
						hr = HRESULT_FROM_WIN32(dwErr);
					}
				}
				else
				{
					hr = E_OUTOFMEMORY;
				}
			}
			else
			{
				hr = HRESULT_FROM_WIN32(dwErr);
			}
		}

		CoTaskMemFree(pwzToProtectCopy);
	}

	return hr;
}

//
// If pwzPassword should be encrypted, return a copy encrypted with CredProtect.
// 
// If not, just return a copy.
//
HRESULT ProtectIfNecessaryAndCopyPassword(
	__in PCWSTR pwzPassword,
	__in CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
	__deref_out PWSTR* ppwzProtectedPassword
)
{
	*ppwzProtectedPassword = nullptr;

	HRESULT hr = E_FAIL;

	// ProtectAndCopyString is intended for non-empty strings only.  Empty passwords
	// do not need to be encrypted.
	if (pwzPassword && *pwzPassword)
	{
		// pwzPassword is const, but CredIsProtected takes a non-const string.
		// So, make a copy that we know isn't const.
		PWSTR pwzPasswordCopy;
		hr = SHStrDupW(pwzPassword, &pwzPasswordCopy);
		if (SUCCEEDED(hr))
		{
			bool bCredAlreadyEncrypted = false;
			CRED_PROTECTION_TYPE protectionType;

			// If the password is already encrypted, we should not encrypt it again.
			// An encrypted password may be received through SetSerialization in the 
			// CPUS_LOGON scenario during a Terminal Services connection, for instance.
			if (CredIsProtectedW(pwzPasswordCopy, &protectionType))
			{
				if (CredUnprotected != protectionType)
				{
					bCredAlreadyEncrypted = true;
				}
			}

			// Passwords should not be encrypted in the CPUS_CREDUI scenario.  We
			// cannot know if our caller expects or can handle an encryped password.
			if (CPUS_CREDUI == cpus || bCredAlreadyEncrypted)
			{
				hr = SHStrDupW(pwzPasswordCopy, ppwzProtectedPassword);
			}
			else
			{
				hr = _ProtectAndCopyString(pwzPasswordCopy, ppwzProtectedPassword);
			}

			SecureZeroMemory(pwzPasswordCopy, sizeof(pwzPasswordCopy));
			CoTaskMemFree(pwzPasswordCopy);
		}
	}
	else
	{
		hr = SHStrDupW(L"", ppwzProtectedPassword);
	}

	return hr;
}

//
// If pwzPassword should be decrypted, return a copy decrypted with CredProtect.
// If not, just return a copy.
HRESULT UnProtectIfNecessaryAndCopyPassword(
	__in PCWSTR pwzPassword,
	__deref_out PWSTR* ppwzUnProtectedPassword
)
{
	*ppwzUnProtectedPassword = nullptr;
	HRESULT hr = E_FAIL;

	if (pwzPassword && *pwzPassword)
	{
		PWSTR pwzPasswordCopy;
		hr = SHStrDupW(pwzPassword, &pwzPasswordCopy);
		if (SUCCEEDED(hr))
		{
			CRED_PROTECTION_TYPE protectionType;

			// Check if the password is encrypted
			if (CredIsProtectedW(pwzPasswordCopy, &protectionType))
			{
				if (CredUnprotected == protectionType)
				{
					hr = SHStrDupW(pwzPasswordCopy, ppwzUnProtectedPassword);
				}
				else
				{
					hr = _UnProtectAndCopyString(pwzPasswordCopy, ppwzUnProtectedPassword);
				}
			}

			CoTaskMemFree(pwzPasswordCopy);
		}
	}
	else
	{
		hr = SHStrDupW(L"", ppwzUnProtectedPassword);
	}

	return hr;
}

//
// Return a copy of pwzToProtect decrypted with the CredProtect API.
//
// pwzToProtect must not be NULL or the empty string.
//
HRESULT _UnProtectAndCopyString(
	__in PCWSTR pwzToUnProtect,
	__deref_out PWSTR* ppwzUnProtected
)
{
	*ppwzUnProtected = NULL;

	// pwzToProtect is const, but CredProtect takes a non-const string.
	// So, make a copy that we know isn't const.
	PWSTR pwzToUnProtectCopy;
	HRESULT hr = SHStrDupW(pwzToUnProtect, &pwzToUnProtectCopy);
	if (SUCCEEDED(hr))
	{
		// The first call to CredProtect determines the length of the encrypted string.
		// Because we pass a NULL output buffer, we expect the call to fail.
		//
		// Note that the third parameter to CredProtect, the number of characters of pwzToProtectCopy
		// to encrypt, must include the NULL terminator!
		DWORD cchUnProtected = 0;
		if (!CredUnprotectW(FALSE, pwzToUnProtectCopy, (DWORD)wcslen(pwzToUnProtectCopy) + 1, NULL, &cchUnProtected))
		{
			DWORD dwErr = GetLastError();

			if ((ERROR_INSUFFICIENT_BUFFER == dwErr) && (0 < cchUnProtected))
			{
				// Allocate a buffer long enough for the encrypted string.
				PWSTR pwzUnProtected = (PWSTR)CoTaskMemAlloc(cchUnProtected * sizeof(WCHAR));
				if (pwzUnProtected)
				{
					// The second call to CredProtect actually encrypts the string.
					if (CredUnprotectW(FALSE, pwzToUnProtectCopy, (DWORD)wcslen(pwzToUnProtectCopy) + 1, pwzUnProtected, &cchUnProtected))
					{
						*ppwzUnProtected = pwzUnProtected;
						hr = S_OK;
					}
					else
					{
						CoTaskMemFree(pwzUnProtected);

						dwErr = GetLastError();
						hr = HRESULT_FROM_WIN32(dwErr);
					}
				}
				else
				{
					hr = E_OUTOFMEMORY;
				}
			}
			else
			{
				hr = HRESULT_FROM_WIN32(dwErr);
			}
		}

		CoTaskMemFree(pwzToUnProtectCopy);
	}

	return hr;
}

//
// Unpack a KERB_INTERACTIVE_UNLOCK_LOGON *in place*.  That is, reset the Buffers from being offsets to
// being real pointers.  This means, of course, that passing the resultant struct across any sort of 
// memory space boundary is not going to work -- repack it if necessary!
//
void KerbInteractiveUnlockLogonUnpackInPlace(
	__inout_bcount(cb) KERB_INTERACTIVE_UNLOCK_LOGON* pkiul,
	__in DWORD cb
)
{
	if (sizeof(*pkiul) <= cb)
	{
		KERB_INTERACTIVE_LOGON* pkil = &pkiul->Logon;

		// Sanity check: if the range described by each (Buffer + MaximumSize) falls within the total bytecount,
		// we can be pretty confident that the Buffers are actually offsets and that this is a packed credential.
		if (((ULONG_PTR)pkil->LogonDomainName.Buffer + pkil->LogonDomainName.MaximumLength <= cb) &&
			((ULONG_PTR)pkil->UserName.Buffer + pkil->UserName.MaximumLength <= cb) &&
			((ULONG_PTR)pkil->Password.Buffer + pkil->Password.MaximumLength <= cb))
		{
			pkil->LogonDomainName.Buffer = pkil->LogonDomainName.Buffer
				? (PWSTR)((BYTE*)pkiul + (ULONG_PTR)pkil->LogonDomainName.Buffer)
				: nullptr;

			pkil->UserName.Buffer = pkil->UserName.Buffer
				? (PWSTR)((BYTE*)pkiul + (ULONG_PTR)pkil->UserName.Buffer)
				: nullptr;

			pkil->Password.Buffer = pkil->Password.Buffer
				? (PWSTR)((BYTE*)pkiul + (ULONG_PTR)pkil->Password.Buffer)
				: nullptr;
		}
	}
}

//
// Use the CredPackAuthenticationBuffer and CredUnpackAuthenticationBuffer to convert a 32 bit WOW
// cred blob into a 64 bit native blob by unpacking it and immediately repacking it.
//
HRESULT KerbInteractiveUnlockLogonRepackNative(
	__in_bcount(cbWow) BYTE* rgbWow,
	__in DWORD cbWow,
	__deref_out_bcount(*pcbNative) BYTE** prgbNative,
	__out DWORD* pcbNative)
{
	HRESULT hr = E_OUTOFMEMORY;
	PWSTR pszDomainUsername = nullptr;
	DWORD cchDomainUsername = 0;
	PWSTR pszPassword = nullptr;
	DWORD cchPassword = 0;

	*prgbNative = nullptr;
	*pcbNative = 0;

	// Unpack the 32 bit KERB structure
	CredUnPackAuthenticationBufferW(CRED_PACK_WOW_BUFFER, rgbWow, cbWow, pszDomainUsername, &cchDomainUsername, NULL, NULL, pszPassword, &cchPassword);
	if (ERROR_INSUFFICIENT_BUFFER == GetLastError())
	{
		pszDomainUsername = (PWSTR)LocalAlloc(0, cchDomainUsername * sizeof(WCHAR));
		if (pszDomainUsername)
		{
			pszPassword = (PWSTR)LocalAlloc(0, cchPassword * sizeof(WCHAR));
			if (pszPassword)
			{
				if (CredUnPackAuthenticationBufferW(CRED_PACK_WOW_BUFFER, rgbWow, cbWow, pszDomainUsername, &cchDomainUsername, NULL, NULL, pszPassword, &cchPassword))
				{
					hr = S_OK;
				}
				else
				{
					hr = GetLastError();
				}
			}
		}
	}

	// Repack native
	if (SUCCEEDED(hr))
	{
		hr = E_OUTOFMEMORY;
		CredPackAuthenticationBufferW(0, pszDomainUsername, pszPassword, *prgbNative, pcbNative);
		if (ERROR_INSUFFICIENT_BUFFER == GetLastError())
		{
			*prgbNative = (BYTE*)LocalAlloc(LMEM_ZEROINIT, *pcbNative);
			if (*prgbNative)
			{
				if (CredPackAuthenticationBufferW(0, pszDomainUsername, pszPassword, *prgbNative, pcbNative))
				{
					hr = S_OK;
				}
				else
				{
					LocalFree(*prgbNative);
				}
			}
		}
	}

	LocalFree(pszDomainUsername);
	if (pszPassword)
	{
		SecureZeroMemory(pszPassword, cchPassword * sizeof(WCHAR));
		LocalFree(pszPassword);
	}
	return hr;
}

// Concatonates pwszDomain and pwszUsername and places the result in *ppwszDomainUsername.
HRESULT DomainUsernameStringAlloc(
	__in PCWSTR pwszDomain,
	__in PCWSTR pwszUsername,
	__deref_out PWSTR* ppwszDomainUsername
)
{
	HRESULT hr = E_FAIL;
	const size_t cchDomain = lstrlen(pwszDomain);
	const size_t cchUsername = lstrlen(pwszUsername);
	// Length of domain, 1 character for '\', length of Username, plus null terminator. 
	const size_t cbLen = sizeof(WCHAR) * (cchDomain + 1 + cchUsername + 1);
	PWSTR pwszDest = (PWSTR)HeapAlloc(GetProcessHeap(), 0, cbLen);
	if (pwszDest)
	{
		hr = StringCbPrintfW(pwszDest, cbLen, L"%s\\%s", pwszDomain, pwszUsername);
		if (SUCCEEDED(hr))
		{
			*ppwszDomainUsername = pwszDest;
		}
		else
		{
			HeapFree(GetProcessHeap(), 0, pwszDest);
		}
	}
	else
	{
		hr = E_OUTOFMEMORY;
	}

	return hr;
}
