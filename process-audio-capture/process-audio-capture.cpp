#include <iostream>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <vector>
#include <TlHelp32.h>
#include <shlobj.h>
#include <wchar.h>
#include <audioclientactivationparams.h>
#include <AudioClient.h>
#include <initguid.h>
#include <guiddef.h>
#include <mfapi.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <atomic>
#include <string>
#include <propsys.h>
#include <propkey.h>
#include <Functiondiscoverykeys_devpkey.h>

#include <wrl\client.h>
#include <wrl\implements.h>
#include <wil\com.h>
#include <wil\result.h>

#include <io.h>
#include <fcntl.h>

using namespace Microsoft::WRL;
using json = nlohmann::json;

#define REFTIMES_PER_SEC 10000000
#define REFTIMES_PER_MILLISEC 10000
REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
REFERENCE_TIME hnsActualDuration;
IAudioCaptureClient* pCaptureClient = nullptr;
wil::com_ptr_nothrow<IAudioCaptureClient> mAudioCaptureClient = nullptr;
WAVEFORMATEX* mCaptureFormat = new WAVEFORMATEX;
UINT32 mBufferFrames;
wil::com_ptr_nothrow<IAudioClient> g_AudioClient;
wil::com_ptr_nothrow<IActivateAudioInterfaceAsyncOperation> asyncOp;
std::thread g_captureThread;

std::atomic<bool> g_isCapturing{ false };
std::atomic<bool> running{ true };

static std::string WideStringToUTF8(const wchar_t* wstr) {
	if (wstr == nullptr || *wstr == L'\0') {
		return "";
	}
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
	if (size_needed == 0) {
		return "";
	}
	std::string str_to(size_needed - 1, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &str_to[0], size_needed, NULL, NULL);
	return str_to;
}

void capture_thread_func() {
	if (mCaptureFormat == nullptr || mCaptureFormat->nSamplesPerSec == 0) {
		std::cerr << "Capture format not initialized. Capture thread exiting.\n";
		return;
	}

	// Calculate the optimal sleep duration (half the buffer size).
	const DWORD sleep_duration_ms = (DWORD)(1000 * mBufferFrames / mCaptureFormat->nSamplesPerSec / 2);

	BYTE* pData;
	UINT32 numFramesAvailable;
	DWORD flags;
	UINT32 packetLength = 0;

	while (g_isCapturing) {
		std::this_thread::sleep_for(std::chrono::milliseconds(sleep_duration_ms));

		if (!mAudioCaptureClient) {
			continue;
		}

		HRESULT hr = mAudioCaptureClient->GetNextPacketSize(&packetLength);
		if (FAILED(hr)) {
			std::cerr << "GetNextPacketSize failed, HRESULT: " << hr << ". Capture thread exiting.\n";
			g_isCapturing = false;
			break;
		}

		while (packetLength != 0)
		{
			hr = mAudioCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
			if (FAILED(hr)) {
				std::cerr << "GetBuffer failed, HRESULT: " << hr << ". Capture thread exiting.\n";
				g_isCapturing = false;
				break;
			}

			size_t bytesToWrite = numFramesAvailable * mCaptureFormat->nBlockAlign;

			if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
			{
				// If silent, write a buffer of zeros.
				std::vector<BYTE> silent_buffer(bytesToWrite, 0);
				std::cout.write(reinterpret_cast<const char*>(silent_buffer.data()), bytesToWrite);
			}
			else
			{
				std::cout.write(reinterpret_cast<const char*>(pData), bytesToWrite);
			}

			std::cout.flush();

			hr = mAudioCaptureClient->ReleaseBuffer(numFramesAvailable);
			if (FAILED(hr)) {
				std::cerr << "ReleaseBuffer failed, HRESULT: " << hr << ". Capture thread exiting.\n";
				g_isCapturing = false;
				break;
			}

			hr = mAudioCaptureClient->GetNextPacketSize(&packetLength);
			if (FAILED(hr)) {
				std::cerr << "GetNextPacketSize (in loop) failed, HRESULT: " << hr << ". Capture thread exiting.\n";
				g_isCapturing = false;
				break;
			}
		}
	}
	std::cerr << "Capture thread finished.\n";
}

class AudioInterfaceCompletionHandler : public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase, IActivateAudioInterfaceCompletionHandler>
{
public:
	wil::com_ptr_nothrow<IAudioClient> m_AudioClient;
	HRESULT m_ActivateCompletedResult = S_OK;
	HRESULT audioClientInitializeResult;
	HRESULT audioClientStartResult;
	HRESULT captureClientResult;
	WAVEFORMATEX m_CaptureFormat{};

	STDMETHOD(ActivateCompleted)
		(IActivateAudioInterfaceAsyncOperation* operation)
	{
		HRESULT hrActivateResult = E_UNEXPECTED;
		wil::com_ptr_nothrow<IUnknown> punkAudioInterface;
		operation->GetActivateResult(&hrActivateResult, &punkAudioInterface);
		if (SUCCEEDED(hrActivateResult))
		{
			m_ActivateCompletedResult = S_OK;
			punkAudioInterface.copy_to(&m_AudioClient);

			m_CaptureFormat.wFormatTag = WAVE_FORMAT_PCM;
			m_CaptureFormat.nChannels = 2;
			m_CaptureFormat.nSamplesPerSec = 48000; //44100
			m_CaptureFormat.wBitsPerSample = 16;    // 32;    //
			m_CaptureFormat.nBlockAlign = m_CaptureFormat.nChannels * m_CaptureFormat.wBitsPerSample / 8;
			m_CaptureFormat.nAvgBytesPerSec = m_CaptureFormat.nSamplesPerSec * m_CaptureFormat.nBlockAlign;

			audioClientInitializeResult = m_AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
				AUDCLNT_STREAMFLAGS_LOOPBACK,       //| AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
				hnsRequestedDuration,               // 200000,
				0,// AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, 
				&m_CaptureFormat,
				NULL);
			if (SUCCEEDED(audioClientInitializeResult))
			{
				std::memcpy(mCaptureFormat, &m_CaptureFormat, sizeof(WAVEFORMATEX));
				captureClientResult = m_AudioClient->GetService(IID_PPV_ARGS(&mAudioCaptureClient));
				audioClientStartResult = m_AudioClient->Start();
				m_AudioClient->GetBufferSize(&mBufferFrames);
				g_AudioClient = m_AudioClient;

				json response;
				response["type"] = "capture-format";
				response["data"]["format"]["sampleRate"] = m_CaptureFormat.nSamplesPerSec;
				response["data"]["format"]["channels"] = m_CaptureFormat.nChannels;
				response["data"]["format"]["bitsPerSample"] = m_CaptureFormat.wBitsPerSample;
				response["data"]["bufferSize"] = mBufferFrames;
				std::cout << response.dump() << std::endl;

				g_isCapturing = true;
				// Start the capture thread
				if (g_captureThread.joinable()) {
					g_captureThread.join();
				}
				g_captureThread = std::thread(capture_thread_func);
			}
			else {
				json response;
				response["type"] = "error";
				response["message"] = "Failed to initialize audio client. HRESULT: " + std::to_string(audioClientInitializeResult);
				std::cout << response.dump() << std::endl;
			}
		}
		else
		{
			m_ActivateCompletedResult = hrActivateResult;
			json response;
			response["type"] = "error";
			response["message"] = "Failed to activate audio interface. HRESULT: " + std::to_string(hrActivateResult);
			std::cout << response.dump() << std::endl;
		}
		return S_OK;
	}
	STDMETHOD(GetActivateCompletedResult)
		(HRESULT* pResult)
	{
		*pResult = m_ActivateCompletedResult;
		return S_OK;
	}
	STDMETHOD(GetAudioClientInitializeResult)
		(HRESULT* pResult)
	{
		*pResult = audioClientInitializeResult;
		return S_OK;
	}
	STDMETHOD(GetCaptureClientResult)
		(HRESULT* pResult)
	{
		*pResult = captureClientResult;
		return S_OK;
	}
	STDMETHOD(GetStartResult)
		(HRESULT* pResult)
	{
		*pResult = audioClientStartResult;
		return S_OK;
	}
	STDMETHOD(GetBufferSize)
		(UINT32* pResult)
	{
		*pResult = mBufferFrames;
		return S_OK;
	}

	~AudioInterfaceCompletionHandler()
	{
	}
};

Microsoft::WRL::ComPtr<AudioInterfaceCompletionHandler> g_completionHandler;


json getAudioSessions() {
	HRESULT hr;
	IMMDeviceEnumerator* pEnumerator = NULL;
	IMMDeviceCollection* pCollection = NULL;
	CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
	pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);

	UINT deviceCount = 0;
	pCollection->GetCount(&deviceCount);
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	json resultArray = json::array();

	for (UINT devIdx = 0; devIdx < deviceCount; ++devIdx) {
		IMMDevice* pDevice = NULL;
		pCollection->Item(devIdx, &pDevice);

		// 获取设备名（支持中文）
		IPropertyStore* pProps = NULL;
		std::string deviceName = "";
		if (SUCCEEDED(pDevice->OpenPropertyStore(STGM_READ, &pProps))) {
			PROPVARIANT varName;
			PropVariantInit(&varName);
			if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &varName))) {
				deviceName = WideStringToUTF8(varName.pwszVal); // 支持中文
				PropVariantClear(&varName);
			}
			pProps->Release();
		}

		IAudioSessionManager2* pSessionManager = NULL;
		IAudioSessionEnumerator* pSessionEnumerator = NULL;
		pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)&pSessionManager);
		pSessionManager->GetSessionEnumerator(&pSessionEnumerator);
		int sessionCount = 0;
		pSessionEnumerator->GetCount(&sessionCount);

		for (int i = 0; i < sessionCount; i++) {
			IAudioSessionControl* pSessionControl = NULL;
			IAudioSessionControl2* pSessionControl2 = NULL;
			hr = pSessionEnumerator->GetSession(i, &pSessionControl);
			if (SUCCEEDED(hr)) {
				hr = pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2);
				if (SUCCEEDED(hr)) {
					// 跳过系统音频会话
					if (pSessionControl2->IsSystemSoundsSession() == S_OK) {
						pSessionControl2->Release();
						pSessionControl->Release();
						continue;
					}
					DWORD processId;
					hr = pSessionControl2->GetProcessId(&processId);
					if (SUCCEEDED(hr)) {
						// 查找进程名（支持中文）
						std::string processName = "";
						PROCESSENTRY32W pe32;
						pe32.dwSize = sizeof(PROCESSENTRY32W);
						if (Process32FirstW(hSnapshot, &pe32)) {
							do {
								if (pe32.th32ProcessID == processId) {
									processName = WideStringToUTF8(pe32.szExeFile); // 支持中文
									break;
								}
							} while (Process32NextW(hSnapshot, &pe32));
						}
						json sessionInfo = {
							{"pid", processId},
							{"processName", processName},
							{"device", deviceName}
						};
						resultArray.push_back(sessionInfo);
					}
					pSessionControl2->Release();
				}
				pSessionControl->Release();
			}
		}
		pSessionEnumerator->Release();
		pSessionManager->Release();
		pDevice->Release();
	}
	CloseHandle(hSnapshot);
	pCollection->Release();
	pEnumerator->Release();

	return resultArray;
}

static bool hasAudioSession(DWORD targetPid) {
	HRESULT hr;
	IMMDeviceEnumerator* pEnumerator = NULL;
	IMMDeviceCollection* pCollection = NULL;
	bool sessionFound = false;

	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
	if (FAILED(hr)) {
		return false;
	}

	hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
	if (FAILED(hr)) {
		pEnumerator->Release();
		return false;
	}

	UINT deviceCount = 0;
	pCollection->GetCount(&deviceCount);

	for (UINT devIdx = 0; devIdx < deviceCount && !sessionFound; ++devIdx) {
		IMMDevice* pDevice = NULL;
		hr = pCollection->Item(devIdx, &pDevice);
		if (FAILED(hr)) continue;

		IAudioSessionManager2* pSessionManager = NULL;
		hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)&pSessionManager);
		if (FAILED(hr)) {
			pDevice->Release();
			continue;
		}

		IAudioSessionEnumerator* pSessionEnumerator = NULL;
		hr = pSessionManager->GetSessionEnumerator(&pSessionEnumerator);
		if (FAILED(hr)) {
			pSessionManager->Release();
			pDevice->Release();
			continue;
		}

		int sessionCount = 0;
		pSessionEnumerator->GetCount(&sessionCount);

		for (int i = 0; i < sessionCount; i++) {
			IAudioSessionControl* pSessionControl = NULL;
			IAudioSessionControl2* pSessionControl2 = NULL;
			hr = pSessionEnumerator->GetSession(i, &pSessionControl);
			if (SUCCEEDED(hr)) {
				hr = pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2);
				if (SUCCEEDED(hr)) {
					if (pSessionControl2->IsSystemSoundsSession() == S_OK) {
						pSessionControl2->Release();
						pSessionControl->Release();
						continue;
					}
					DWORD processId;
					hr = pSessionControl2->GetProcessId(&processId);
					if (SUCCEEDED(hr) && processId == targetPid) {
						sessionFound = true;
						pSessionControl2->Release();
						pSessionControl->Release();
						break;
					}
					pSessionControl2->Release();
				}
				pSessionControl->Release();
			}
		}
		pSessionEnumerator->Release();
		pSessionManager->Release();
		pDevice->Release();
	}

	pCollection->Release();
	pEnumerator->Release();

	return sessionFound;
}

void initializeCLoopbackCapture(DWORD pid) {
	HRESULT hr;

	bool includeProcessTree = true;
	AUDIOCLIENT_ACTIVATION_PARAMS audioclientActivationParams = {};
	audioclientActivationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
	audioclientActivationParams.ProcessLoopbackParams.ProcessLoopbackMode = includeProcessTree ? PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE : PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
	audioclientActivationParams.ProcessLoopbackParams.TargetProcessId = pid;

	PROPVARIANT activateParams = {};
	activateParams.vt = VT_BLOB;
	activateParams.blob.cbSize = sizeof(audioclientActivationParams);
	activateParams.blob.pBlobData = (BYTE*)&audioclientActivationParams;

	g_completionHandler = Make<AudioInterfaceCompletionHandler>();
	// Microsoft::WRL::ComPtr<AudioInterfaceCompletionHandler> completionHandler = Make<AudioInterfaceCompletionHandler>();
	hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
		__uuidof(IAudioClient),
		&activateParams,
		g_completionHandler.Get(),
		// completionHandler.Get(),
		&asyncOp);

	if (FAILED(hr)) {
		json response;
		response["type"] = "error";
		response["message"] = "ActivateAudioInterfaceAsync failed immediately. HRESULT: " + std::to_string(hr);
		std::cout << response.dump() << std::endl;
	}
}



void handleJSONInput(json parsed) {
	if (parsed.contains("type")) {
		std::string type = parsed["type"];

		if (type == "quit") {
			running = false;
			std::cerr << "quit command received\n";
		}
		else if (type == "get-audio-sessions") {
			json result = getAudioSessions();
			json response;
			response["type"] = "audio-sessions";
			response["data"] = result;
			std::cout << response << std::endl;
		}
		else if (type == "stop-capture") {
			if (g_isCapturing) {
				g_isCapturing = false;
				if (g_captureThread.joinable()) {
					g_captureThread.join();
				}
				// clear the pointers
				g_AudioClient = nullptr;
				mAudioCaptureClient = nullptr;
				g_completionHandler = nullptr;

				json response;
				response["type"] = "capture-stopped";
				std::cout << response.dump() << std::endl;
			}
			else {
				json response;
				response["type"] = "error";
				response["message"] = "Capture is not in progress.";
				std::cout << response.dump() << std::endl;
			}
		}
		else if (type == "start-capture") {
			if (g_isCapturing) {
				json response;
				response["type"] = "error";
				response["message"] = "Capture is already in progress. Please stop the current capture first.";
				std::cout << response.dump() << std::endl;
				return;
			}
			if (parsed.contains("pid")) {
				if (parsed["pid"].is_number_unsigned()) {
					DWORD pid = parsed["pid"];
					if (hasAudioSession(pid)) {
						initializeCLoopbackCapture(pid);
					}
					else {
						json response;
						response["type"] = "error";
						response["message"] = "Process with PID " + std::to_string(pid) + " has no active audio session or does not exist.";
						std::cout << response.dump() << std::endl;
					}
				}
				else {
					json response;
					response["type"] = "error";
					response["message"] = "pid must be an unsigned number.";
					std::cout << response.dump() << std::endl;
				}
			}
			else {
				json response;
				response["type"] = "error";
				response["message"] = "pid is required when using start-capture command.";
				std::cout << response.dump() << std::endl;
			}
		}
		else {
			std::cerr << "unknown type: " << type << "\n";
		}
	}
	else {
		std::cerr << "JSON does not contain type field\n";
	}
}


void input_thread_func() {
	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr)) {
		std::cerr << "Failed to initialize COM for input thread. HRESULT: " << hr << std::endl;
		return;
	}

	std::string line;
	while (running) {
		if (std::getline(std::cin, line)) {
			try {
				auto parsed = json::parse(line);
				handleJSONInput(parsed);
			}
			catch (const std::exception& e) {
				std::cerr << "JSON parse failed: " << e.what() << "\n";
			}
		}
		else {
			running = false;
		}
	}
	CoUninitialize();
}

int main()
{
	_setmode(_fileno(stdout), _O_BINARY);
	//std::cerr << "Hello World!\n";


	std::thread input_thread(input_thread_func);

	// 主线程等待直到收到 quit
	while (running) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	if (g_captureThread.joinable()) g_captureThread.join();
	if (input_thread.joinable()) input_thread.join();
	return 0;
}
