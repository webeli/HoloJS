#include "pch.h"
#include "ScriptsLoader.h"
#include "XmlHttpRequest.h"

using namespace HologramJS::API;
using namespace Windows::Foundation;
using namespace Windows::Web::Http;
using namespace HologramJS::Utilities;
using namespace concurrency;
using namespace std;

bool XmlHttpRequest::UseFileSystem = false;
wstring XmlHttpRequest::BaseUrl = L"";
wstring XmlHttpRequest::BasePath = L"";
JsValueRef XmlHttpRequest::m_createXHRFunction = JS_INVALID_REFERENCE;
JsValueRef XmlHttpRequest::m_getResponseFunction = JS_INVALID_REFERENCE;
JsValueRef XmlHttpRequest::m_getResponseTextFunction = JS_INVALID_REFERENCE;
JsValueRef XmlHttpRequest::m_sendXHRFunction = JS_INVALID_REFERENCE;
JsValueRef XmlHttpRequest::m_getHeaderFunction = JS_INVALID_REFERENCE;
JsValueRef XmlHttpRequest::m_setHeaderFunction = JS_INVALID_REFERENCE;

XmlHttpRequest::XmlHttpRequest()
{
}

bool
XmlHttpRequest::Initialize()
{
	RETURN_IF_FALSE(ScriptHostUtilities::ProjectFunction(L"create", L"xhr", createXHR, nullptr, &m_createXHRFunction));
	RETURN_IF_FALSE(ScriptHostUtilities::ProjectFunction(L"send", L"xhr", sendXHR, nullptr, &m_sendXHRFunction));
	RETURN_IF_FALSE(ScriptHostUtilities::ProjectFunction(L"getResponse", L"xhr", getResponse, nullptr, &m_getResponseFunction));
	RETURN_IF_FALSE(ScriptHostUtilities::ProjectFunction(L"getResponseText", L"xhr", getResponseText, nullptr, &m_getResponseTextFunction));
	RETURN_IF_FALSE(ScriptHostUtilities::ProjectFunction(L"getHeader", L"xhr", getHeader, nullptr, &m_getHeaderFunction));
	RETURN_IF_FALSE(ScriptHostUtilities::ProjectFunction(L"setHeader", L"xhr", setHeader, nullptr, &m_setHeaderFunction));

	return true;
}

_Use_decl_annotations_
JsValueRef
CHAKRA_CALLBACK
XmlHttpRequest::createXHR(
	JsValueRef callee,
	bool isConstructCall,
	JsValueRef *arguments,
	unsigned short argumentCount,
	PVOID callbackData
)
{
	ExternalObject* externalObject = new ExternalObject();
	RETURN_INVALID_REF_IF_FALSE(externalObject->Initialize(new XmlHttpRequest()));
	return ScriptResourceTracker::ObjectToDirectExternal(externalObject);
}

_Use_decl_annotations_
JsValueRef
CHAKRA_CALLBACK
XmlHttpRequest::setHeader(
	JsValueRef callee,
	bool isConstructCall,
	JsValueRef *arguments,
	unsigned short argumentCount,
	PVOID callbackData
)
{
	RETURN_INVALID_REF_IF_FALSE(argumentCount == 4);
	auto xhr = ScriptResourceTracker::ExternalToObject<XmlHttpRequest>(arguments[1]);
	RETURN_INVALID_REF_IF_NULL(xhr);

	wstring header;
	RETURN_INVALID_REF_IF_FALSE(ScriptHostUtilities::GetString(arguments[2], header));

	wstring value;
	RETURN_INVALID_REF_IF_FALSE(ScriptHostUtilities::GetString(arguments[3], value));

	xhr->m_requestHeaders.emplace_back(header, value);

	return JS_INVALID_REFERENCE;
}

_Use_decl_annotations_
JsValueRef
CHAKRA_CALLBACK
XmlHttpRequest::getHeader(
	JsValueRef callee,
	bool isConstructCall,
	JsValueRef *arguments,
	unsigned short argumentCount,
	PVOID callbackData
)
{
	RETURN_INVALID_REF_IF_FALSE(argumentCount == 3);
	auto xhr = ScriptResourceTracker::ExternalToObject<XmlHttpRequest>(arguments[1]);
	RETURN_INVALID_REF_IF_NULL(xhr);

	wstring header;
	RETURN_INVALID_REF_IF_FALSE(ScriptHostUtilities::GetString(arguments[2], header));

	auto headerRef = Platform::StringReference(header.c_str());
	if (xhr->m_responseHeaders->HasKey(headerRef))
	{
		auto valueRef = xhr->m_responseHeaders->Lookup(headerRef);
		JsValueRef returnValue;
		RETURN_INVALID_REF_IF_JS_ERROR(JsPointerToString(valueRef->Data(), valueRef->Length(), &returnValue));

		return returnValue;
	}
	else
	{
		return JS_INVALID_REFERENCE;
	}
}

_Use_decl_annotations_
JsValueRef
CHAKRA_CALLBACK
XmlHttpRequest::sendXHR(
	JsValueRef callee,
	bool isConstructCall,
	JsValueRef *arguments,
	unsigned short argumentCount,
	PVOID callbackData
)
{
	RETURN_INVALID_REF_IF_FALSE(argumentCount == 5);
	auto xhr = ScriptResourceTracker::ExternalToObject<XmlHttpRequest>(arguments[1]);
	RETURN_INVALID_REF_IF_NULL(xhr);

	wstring method;
	RETURN_INVALID_REF_IF_FALSE(ScriptHostUtilities::GetString(arguments[2], method));

	wstring uri;
	RETURN_INVALID_REF_IF_FALSE(ScriptHostUtilities::GetString(arguments[3], uri));

	wstring type;
	RETURN_INVALID_REF_IF_FALSE(ScriptHostUtilities::GetString(arguments[4], type));

	JsValueType valueType;
	JsGetValueType(arguments[5], &valueType);

	xhr->SendRequest(method, uri, type);

	return JS_INVALID_REFERENCE;
}

_Use_decl_annotations_
JsValueRef
CHAKRA_CALLBACK
XmlHttpRequest::getResponseText(
	JsValueRef callee,
	bool isConstructCall,
	JsValueRef *arguments,
	unsigned short argumentCount,
	PVOID callbackData
)
{
	RETURN_INVALID_REF_IF_FALSE(argumentCount == 2);
	auto xhr = ScriptResourceTracker::ExternalToObject<XmlHttpRequest>(arguments[1]);
	RETURN_INVALID_REF_IF_NULL(xhr);

	RETURN_INVALID_REF_IF_FALSE(xhr->IsTextResponse());

	JsValueRef returnValue;
	RETURN_INVALID_REF_IF_JS_ERROR(JsPointerToString(xhr->m_responseText.c_str(), xhr->m_responseText.length(), &returnValue));

	return returnValue;
}

_Use_decl_annotations_
JsValueRef
CHAKRA_CALLBACK
XmlHttpRequest::getResponse(
	JsValueRef callee,
	bool isConstructCall,
	JsValueRef *arguments,
	unsigned short argumentCount,
	PVOID callbackData
)
{
	RETURN_INVALID_REF_IF_FALSE(argumentCount == 2);
	auto xhr = ScriptResourceTracker::ExternalToObject<XmlHttpRequest>(arguments[1]);
	RETURN_INVALID_REF_IF_NULL(xhr);

	if (xhr->IsTextResponse())
	{
		return getResponseText(callee, isConstructCall, arguments, argumentCount, callbackData);
	}
	else
	{
		Microsoft::WRL::ComPtr<Windows::Storage::Streams::IBufferByteAccess> bufferByteAccess;
		RETURN_INVALID_REF_IF_FAILED(reinterpret_cast<IInspectable*>(xhr->m_response)->QueryInterface(IID_PPV_ARGS(&bufferByteAccess)));

		byte* responseNativeBuffer;
		RETURN_INVALID_REF_IF_FAILED(bufferByteAccess->Buffer(&responseNativeBuffer));

		JsValueRef returnArray;
		void* responesExternalBuffer;
		RETURN_INVALID_REF_IF_JS_ERROR(ScriptResourceTracker::CreateAndTrackExternalBuffer(xhr->m_responseLength, &responesExternalBuffer, &returnArray));

		CopyMemory(responesExternalBuffer, responseNativeBuffer, xhr->m_responseLength);

		return returnArray;
	}
}

void
XmlHttpRequest::SendRequest(
	const std::wstring& method,
	const std::wstring& uri,
	const std::wstring type
)
{
	m_method = method;
	m_url = uri;
	m_state = RequestState::OPENED;
	m_responseType = type;

	if (m_url.empty() || (_wcsicmp(m_method.c_str(), L"get") != 0))
	{
		return;
	}

	if ((_wcsnicmp(m_url.c_str(), L"http://", wcslen(L"http://")) == 0)
		|| (_wcsnicmp(m_url.c_str(), L"https://", wcslen(L"https://")) == 0)
		|| !UseFileSystem)
	{
		DownloadAsync();
	}
	else
	{
		ReadFromPackageAsync();
	}
}

task<void>
XmlHttpRequest::ReadFromPackageAsync()
{
	wstring completePath = BasePath + m_url;
	auto mainPathElements = HologramJS::ScriptsLoader::SplitPath(completePath);

	if (mainPathElements.size() == 0)
	{
		return;
	}

	auto fileName = mainPathElements.front();
	mainPathElements.erase(mainPathElements.begin());

	if (IsTextResponse())
	{
		auto responsePlatformString = await HologramJS::ScriptsLoader::ReadTextFromFile(
			Windows::ApplicationModel::Package::Current->InstalledLocation,
			mainPathElements,
			fileName);
		m_responseText = responsePlatformString->Data();
		m_responseType.assign(L"text");
	}
	else // arraybuffer or blob
	{
		m_response = await HologramJS::ScriptsLoader::ReadBinaryFromFile(
			Windows::ApplicationModel::Package::Current->InstalledLocation,
			mainPathElements,
			fileName);

		m_responseLength = m_response->Length;
	}

	m_state = RequestState::DONE;
	m_status = 200;
	m_statusText = L"OK";

	FireStateChanged();
}

task<void>
XmlHttpRequest::DownloadAsync()
{
	Windows::Foundation::Uri^ uri;

	if ((_wcsnicmp(m_url.c_str(), L"http://", wcslen(L"http://")) == 0)
		|| (_wcsnicmp(m_url.c_str(), L"https://", wcslen(L"https://")) == 0))
	{
		uri = ref new Windows::Foundation::Uri(Platform::StringReference(m_url.c_str()));
	}
	else
	{
		wstring completeUrl = BaseUrl + m_url;
		uri = ref new Windows::Foundation::Uri( Platform::StringReference(completeUrl.c_str()));
	}

	Windows::Web::Http::HttpClient^ httpClient = ref new  Windows::Web::Http::HttpClient();

	for (const auto& headerPair : m_requestHeaders)
	{
		httpClient->DefaultRequestHeaders->Append(Platform::StringReference(headerPair.first.c_str()), Platform::StringReference(headerPair.second.c_str()));
	}

	if (_wcsicmp(m_method.c_str(), L"get") == 0)
	{
		HttpResponseMessage^ responseMessage;
		try
		{
			responseMessage = await httpClient->GetAsync(uri);
		}
		catch (...)
		{
			m_status = -1;
		}

		if (responseMessage)
		{
			if (responseMessage->IsSuccessStatusCode)
			{
				if (IsTextResponse())
				{
					auto responseText = await responseMessage->Content->ReadAsStringAsync();
					m_responseText.assign(responseText->Data());
				}
				else
				{
					m_response = await responseMessage->Content->ReadAsBufferAsync();
					m_responseLength = m_response->Length;
				}

				m_status = 200;
				m_statusText = L"OK";
				m_responseHeaders = responseMessage->Headers;
			}
			else
			{
				m_status = static_cast<int>(responseMessage->StatusCode);
				m_statusText.assign(responseMessage->ReasonPhrase->Data());
			}
		}
	}

	m_state = RequestState::DONE;

	FireStateChanged();

	return;
}

void
XmlHttpRequest::FireStateChanged()
{
	if (HasCallback())
	{
		vector<JsValueRef> parameters(5);
		JsValueRef* typeParam = &parameters[0];
		EXIT_IF_JS_ERROR(JsPointerToString(L"change", wcslen(L"change"), typeParam));

		JsValueRef* stateParam = &parameters[1];
		EXIT_IF_JS_ERROR(JsIntToNumber(static_cast<int>(m_state), stateParam));

		JsValueRef* statusParam = &parameters[2];
		EXIT_IF_JS_ERROR(JsIntToNumber(static_cast<int>(m_status), statusParam));

		JsValueRef* statusTextParam = &parameters[3];
		EXIT_IF_JS_ERROR(JsPointerToString(m_statusText.c_str(), m_statusText.length(), statusTextParam));

		JsValueRef* responesTypeParam = &parameters[4];
		EXIT_IF_JS_ERROR(JsPointerToString(m_responseType.c_str(), m_responseType.length(), responesTypeParam));

		(void)InvokeCallback(parameters);
	}
}