#include "pch.h"
#include "httpclient.h"

#include "squirrel.h"
#include <iostream>

// Haven't implemented status codes and headers yet.
struct HTTPResponse
{
	const char* body;
};

struct HTTPRequest
{
	int id;
	const char* url;
	const char* method;
	const char* body;
	bool resolved = false;
	HTTPResponse response;
};

void SetCommonHttpClientOptions(CURL* curl)
{
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
}

size_t CurlWriteToStringBufferCallbackLocal(char* contents, size_t size, size_t nmemb, void* userp)
{
	((std::string*)userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}

char* MakePost(const char* url, const char* body)
{
	CURL* curl = curl_easy_init();

	std::string readBuffer;

	curl_easy_setopt(
		curl,
		CURLOPT_URL,
		url
		);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallbackLocal);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	SetCommonHttpClientOptions(curl);

	CURLcode res = curl_easy_perform(curl);

	if (res != CURLE_OK)
		fprintf(
			stderr, "curl_easy_perform() failed: %s\n",
			curl_easy_strerror(res)
			);

	curl_easy_cleanup(curl);



	char* writable = new char[readBuffer.size() + 1];
	std::copy(readBuffer.begin(), readBuffer.end(), writable);
	writable[readBuffer.size()] = '\0';

	response.body = writable;
	requests[reqIndex].resolved = true;
	requests[reqIndex].response = response;
}

HTTPRequest* GetRequestByID(int id)
{
	for (auto r : requests)
	{
		if (r.id == id)
			return &r;
	}
	return nullptr;
}

SQRESULT SQ_SendHTTPRequest(void* sqvm)
{
	const char* url = ServerSq_getstring(sqvm, 1);
	const char* method = ServerSq_getstring(sqvm, 2);
	const char* body = ServerSq_getstring(sqvm, 3);

	HTTPRequest request;
	request.id = lastID;
	request.url = url;
	request.method = method;
	request.body = body;

	requests.push_back(request);
	std::thread requestThread(MakeRequest, url, method, body, lastID);
	requestThread.detach();
	ServerSq_pushinteger(sqvm, lastID);

	lastID++;
	return SQRESULT_NOTNULL;
}

// bool function NSHTTPIsResolved( int reqID )
SQRESULT SQ_IsResolved(void* sqvm)
{
	int reqID = ServerSq_getinteger(sqvm, 1);
	HTTPRequest* request = GetRequestByID(reqID);
	if (request == nullptr || !request->response.body)
	{
		ServerSq_pusherror(sqvm, fmt::format("No request with ID {} exists or request isn't resolved", reqID).c_str());
		return SQRESULT_ERROR;
	}
	else
		ServerSq_pushbool(sqvm, request->resolved);
	return SQRESULT_NOTNULL;
}

// string function NSGetResponseBody( int reqID )
SQRESULT SQ_GetResponseBody(void* sqvm)
{
	int reqID = ServerSq_getinteger(sqvm, 1);
	HTTPRequest* request = GetRequestByID(reqID);
	if (request == nullptr || !request->response.body)
	{
		ServerSq_pusherror(sqvm, fmt::format("No request with ID {} exists or request isn't resolved", reqID).c_str());
		return SQRESULT_ERROR;
	}
	else
		ServerSq_pushstring(sqvm, request->response.body, -1);
	return SQRESULT_NOTNULL;
}

// void function NSRemoveRequest( int reqID )
SQRESULT SQ_RemoveRequest(void* sqvm)
{
	int reqID = ServerSq_getinteger(sqvm, 1);
	HTTPRequest* request = GetRequestByID(reqID);
	if (request == nullptr)
	{
		ServerSq_pusherror(sqvm, fmt::format("No request with ID {} exists", reqID).c_str());
		return SQRESULT_ERROR;
	}
	else
	{
		int i = 0;
		for (auto r : requests)
			if (r.id == reqID)
			{
				requests.erase(requests.begin() + i);
				break;
			}
	}
	return SQRESULT_NULL;
}
void InitialiseHttpClient(HMODULE baseAddress)
{
	g_ServerSquirrelManager->AddFuncRegistration("int", "NSHTTPRequest", "string url, string method, string body", "", SQ_SendHTTPRequest);
	g_ServerSquirrelManager->AddFuncRegistration("string", "NSGetResponseBody", "int reqID", "", SQ_GetResponseBody);
	//g_ServerSquirrelManager->AddFuncRegistration("int", "NSGetResponseStatus", "int index", "", SQ_GetResponseCode);
	g_ServerSquirrelManager->AddFuncRegistration("bool", "NSHTTPIsResolved", "int reqID", "", SQ_IsResolved);
	g_ServerSquirrelManager->AddFuncRegistration("void", "NSRemoveRequest", "int reqID", "", SQ_RemoveRequest);
}
