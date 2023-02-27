#include "engine.h"

#include <Windows.h>

#include <format>
#include <string>
#include <array>

#include "utils.h"

using namespace std::string_view_literals;

void SysError(std::string_view msg)
{
}


class PipeHandle final
{
public:
	PipeHandle() = default;
	PipeHandle(const PipeHandle&) = delete;
	PipeHandle(PipeHandle&&) = default;
	PipeHandle& operator=(const PipeHandle&) = delete;
	PipeHandle& operator=(PipeHandle&&) = default;
	~PipeHandle()
	{
		if ((m_handle != INVALID_HANDLE_VALUE) && (m_handle != nullptr))
		{
			CloseHandle(m_handle);
			m_handle = INVALID_HANDLE_VALUE;
		}
	}
	
	HANDLE& get() { return m_handle; }

private:
	HANDLE m_handle = INVALID_HANDLE_VALUE;
};


// Run a console command, with optional redirection of stdout and stderr to our log
std::string RunCommand(std::string_view cmd, std::string_view dir, bool log)
{
	constexpr DWORD dwPipeSize = 4096*8;
	STARTUPINFO si = { .cb = sizeof(si) };
	PROCESS_INFORMATION pi = {};
	PipeHandle hOutputRead;
	PipeHandle hOutputWrite;
	std::string output;

	if (log)
	{
		SECURITY_ATTRIBUTES sa = { .nLength = static_cast<DWORD>(sizeof sa), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE };
		// NB: The size of a pipe is a suggestion, NOT an absolute guarantee
		// This means that you may get a pipe of 4K even if you requested 1K
		if (!CreatePipe(&hOutputRead.get(), &hOutputWrite.get(), &sa, dwPipeSize))
		{
			SysError(std::format("Could not set commandline pipe: {}"sv, GetLastError()));
			return output;
		}
		si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES | STARTF_PREVENTPINNING | STARTF_TITLEISAPPID;
		si.wShowWindow = SW_HIDE;
		si.hStdOutput = hOutputWrite.get();
		si.hStdError = hOutputWrite.get();
	}

	if (!CreateProcess(nullptr, va(cmd), nullptr, nullptr, TRUE, NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW, nullptr, dir.data(), &si, &pi))
	{
		SysError(std::format("Unable to launch command '{}': {}", cmd, GetLastError()));
		return output;
	}

	if (log)
	{
		while (true)
		{
			// coverity[string_null]
			DWORD dwAvail;
			if (PeekNamedPipe(hOutputRead.get(), nullptr, dwPipeSize, nullptr, &dwAvail, nullptr))
			{
				if (dwAvail != 0)
				{
					DWORD dwRead;
					std::array<char, dwPipeSize> buf{};
					//output = malloc(dwAvail + 1);
					//std::unique_ptr<char[]> buf(new char[dwAvail + 1]);
					if (ReadFile(hOutputRead.get(), buf.data(), dwAvail, &dwRead, nullptr) && dwRead != 0)
					{
						//buf[dwAvail] = '\0';
						// coverity[tainted_string]
						//uprintf(output);
						output.assign(buf.data(), dwRead);
					}
					//free(output);
				}
			}
			if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0)
				break;
			Sleep(100);
		}
	}
	else
	{
		WaitForSingleObject(pi.hProcess, INFINITE);
	}

	DWORD ret;
	if (!GetExitCodeProcess(pi.hProcess, &ret))
		ret = GetLastError();
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	
	return output;
}

