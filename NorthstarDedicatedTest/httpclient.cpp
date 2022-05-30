#include "pch.h"
#include "httpclient.h"

#include "squirrel.h"
#include <iostream>
#include <optional>
#include <utility>


struct HTTPResponse
{
	//long status;
	//std::vector<const char*> headers;
	std::string body;
};


struct HTTPRequestData
{
	const std::string url;
	const std::string method;
	const std::string body;
};


struct HTTPRequest
{
	int id;
	std::optional<HTTPResponse> response;
};


int lastID = 0;

std::vector<HTTPRequest*> requests;

void SetCommonHttpClientOptions(CURL* curl)
{
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);


	struct curl_slist* headers = NULL;
	headers = curl_slist_append(headers, "Accept: application/json");
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, "charset: utf-8");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
}

// i don't know actual what this stuff do
size_t CurlWriteToStringBufferCallbackLocal(char* contents, size_t size, size_t nmemb, void* userp)
{
	((std::string*)userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}


HTTPResponse MakeRequestSync(const HTTPRequestData data)
{
	CURL* curl = curl_easy_init();

	std::string readBuffer;
	long http_code;
	curl_easy_setopt(curl, CURLOPT_URL, data.url.c_str());
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, data.method.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallbackLocal);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.body.c_str());
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

	SetCommonHttpClientOptions(curl);

	CURLcode res = curl_easy_perform(curl);

	if (res != CURLE_OK)
		fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

	curl_easy_cleanup(curl);

	return HTTPResponse{readBuffer};
}

HTTPRequestData ParseSquirrelInput(void* sqvm)
{
	const char* url = ServerSq_getstring(sqvm, 1);
	const char* method = ServerSq_getstring(sqvm, 2);
	const char* body = ServerSq_getstring(sqvm, 3);

	return HTTPRequestData{url, method, body};
}

int GetRequestIndexById(int id)
{
	for (int i = 0; i < requests.size(); i++)
	{
		if (requests[i]->id == id)
			return i;
	}
	return -1;
}

HTTPRequest* GetRequestByID(int id)
{
	for (HTTPRequest* r : requests)
	{
		if (r->id == id)
			return r;
	}
	return nullptr;
}

void MakeTrackingRequestThread(HTTPRequestData data, HTTPRequest* requestPointer)
{
	HTTPResponse responsePointer = MakeRequestSync(std::move(data));
	requestPointer->response = responsePointer;
}

SQRESULT SQ_SendHTTPRequestTracking(void* sqvm)
{
	HTTPRequestData data = ParseSquirrelInput(sqvm);

	int currentID = lastID++;

	HTTPRequest* request = new HTTPRequest{currentID, std::nullopt};
	requests.push_back(request);
	std::thread requestThread(MakeTrackingRequestThread, data, request);
	requestThread.detach();

	ServerSq_pushinteger(sqvm, currentID);

	return SQRESULT_NOTNULL;
}

SQRESULT SQ_SendHTTPRequestNoTracking(void* sqvm)
{
	HTTPRequestData data = ParseSquirrelInput(sqvm);

	std::thread requestThread(MakeRequestSync, data);
	requestThread.detach();

	return SQRESULT_NULL;
}


SQRESULT SQ_GetResponseIfComplete(void* sqvm)
{
	int reqID = ServerSq_getinteger(sqvm, 1);
	int requestIndex = GetRequestIndexById(reqID);
	if (requestIndex == -1)
	{
		ServerSq_pusherror(sqvm, fmt::format("No request with ID {} exists or request already received", reqID).c_str());
		return SQRESULT_ERROR;
	}
	else
	{
		SQRESULT result;
		auto response = requests[requestIndex];
		spdlog::info("response id: {}", response->id);

		if (response->response.has_value())
		{
			std::string string = response->response.value().body;

			char* writable = new char[string.size() + 1];
			std::copy(string.begin(), string.end(), writable);
			writable[string.size()] = '\0';

			ServerSq_pushstring(sqvm, writable, -1);
			result = SQRESULT_NOTNULL;

			requests.erase(requests.begin() + requestIndex);
			delete response;
		}
		else
		{
			result = SQRESULT_NULL;
		}
		return result;
	}
}

void InitialiseHttpClient(HMODULE baseAddress)
{
	// Request with a response
	g_ServerSquirrelManager->AddFuncRegistration(
		"int", "NSHTTPRequestTracking", "string url, string method, string body", "", SQ_SendHTTPRequestTracking);
	// Request to send and forget
	g_ServerSquirrelManager->AddFuncRegistration(
		"int", "NSHTTPRequest", "string url, string method, string body", "", SQ_SendHTTPRequestNoTracking);
	g_ServerSquirrelManager->AddFuncRegistration("string", "NSGetResponseIfComplete", "int reqID", "", SQ_GetResponseIfComplete);
}
