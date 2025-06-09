#pragma once

#ifndef _WIN32
#error This library is for Windows only.
#endif

#include <windows.h>
#include <winreg.h>

#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <memory>
#include <format>
#include <iostream>
#include <system_error>
#include <array>

namespace deeplink {

    namespace ipc 
    {
        class IIpcMechanism 
        {
        public:
            virtual ~IIpcMechanism() = default;
            virtual bool isServerRunning() const = 0;
            virtual void sendMessage(const std::string& sMessage) const = 0;
            virtual void startServer(std::function<void(const std::string&)> fOnMessage) = 0;
            virtual void stopServer() = 0;
        };

        class NamedPipeIpcMechanism : public IIpcMechanism {
        public:
            explicit NamedPipeIpcMechanism(const std::wstring& wsUniqueId) : m_wsPipeName(L"\\\\.\\pipe\\" + wsUniqueId)
            {
            }

            ~NamedPipeIpcMechanism() override 
            {
                stopServer();
            }

            bool isServerRunning() const override 
            {
                return WaitNamedPipeW(m_wsPipeName.c_str(), 0) || (GetLastError() == ERROR_PIPE_BUSY);
            }

            void sendMessage(const std::string& sMessage) const override 
            {
                HANDLE hPipe = CreateFileW(
                    m_wsPipeName.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

                if (hPipe != INVALID_HANDLE_VALUE) 
                {
                    DWORD dwBytesWritten;
                    WriteFile(hPipe, sMessage.c_str(), (DWORD)sMessage.length(), &dwBytesWritten, NULL);
                    CloseHandle(hPipe);
                }
            }

            void startServer(std::function<void(const std::string&)> fOnMessage) override 
            {
                m_fOnMessage = std::move(fOnMessage);
                m_bStopFlag = false;
                m_tServerThread = std::thread([this]() { this->serverLoop(); });
            }

            void stopServer() override 
            {
                if (!m_bStopFlag.exchange(true)) 
                {
                    HANDLE hPipe = CreateFileW(m_wsPipeName.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
                    if (hPipe != INVALID_HANDLE_VALUE) 
                        CloseHandle(hPipe);

                    if (m_tServerThread.joinable()) 
                        m_tServerThread.join();
                }
            }

        private:
            void serverLoop() 
            {
                while (!m_bStopFlag) 
                {
                    HANDLE hPipe = CreateNamedPipeW( m_wsPipeName.c_str(), PIPE_ACCESS_INBOUND,
                        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                        PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, NULL);

                    if (hPipe == INVALID_HANDLE_VALUE) 
                        continue;

                    BOOL bConnected = ConnectNamedPipe(hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
                    if (bConnected && !m_bStopFlag) 
                    {
                        std::array<char, 2048> arrBuffer = {};
                        DWORD dwBytesRead;
                        if (ReadFile(hPipe, arrBuffer.data(), sizeof(arrBuffer) - 1, &dwBytesRead, NULL) && dwBytesRead > 0) 
                        {
                            arrBuffer[dwBytesRead] = '\0';
                            if (m_fOnMessage)
                                m_fOnMessage(std::string(arrBuffer.data()));
                        }
                    }

                    DisconnectNamedPipe(hPipe);
                    CloseHandle(hPipe);
                }
            }

            std::wstring m_wsPipeName;
            std::function<void(const std::string&)> m_fOnMessage;
            std::thread m_tServerThread;
            std::atomic<bool> m_bStopFlag = false;
        };
    } // namespace ipc


    template <typename IpcStrategy = ipc::NamedPipeIpcMechanism>
    class DeepLink {
    public:
        explicit DeepLink(std::wstring wsScheme)  : m_wsScheme(std::move(wsScheme)), m_pIpc(std::make_unique<IpcStrategy>(m_wsScheme)) 
        {
        }

        ~DeepLink() 
        {
            if (m_pIpc)
                m_pIpc->stopServer();
        }

        DeepLink(const DeepLink&) = delete;
        DeepLink& operator=(const DeepLink&) = delete;

        void setOnMessage(std::function<void(const std::string&)> fOnMessage) 
        {
            m_fOnMessage = std::move(fOnMessage);
        }

        void registerScheme() const 
        {
            auto closeKey = [](HKEY hKey) { if (hKey) RegCloseKey(hKey); };
            using RegKeyPtr = std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(closeKey)>;

            try 
            {
                std::array<wchar_t, MAX_PATH> arrExePathBuf;
                if (GetModuleFileNameW(NULL, arrExePathBuf.data(), MAX_PATH) == 0)
                    throw std::system_error(GetLastError(), std::system_category(), "getModuleFileNameW failed");

                std::wstring wsExePath = arrExePathBuf.data();

                const std::wstring wsRegPath = L"Software\\Classes\\" + m_wsScheme;
                const std::wstring wsUrlProtocolValue = L"URL:" + m_wsScheme;
                const std::wstring wsCommandValue = std::format(L"\"{}\" \"%1\"", wsExePath);
                const std::wstring wsIconValue = std::format(L"{},0", wsExePath);

                HKEY hSchemeKey;
                LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, wsRegPath.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hSchemeKey, NULL);
                if (status != ERROR_SUCCESS) 
                    throw std::system_error(status, std::system_category(), "failed to create scheme root key");
                RegKeyPtr schemeKeyGuard(hSchemeKey, closeKey);

                status = RegSetValueExW(hSchemeKey, NULL, 0, REG_SZ, (const BYTE*)wsUrlProtocolValue.c_str(), (DWORD)((wsUrlProtocolValue.size() + 1) * sizeof(wchar_t)));
                if (status != ERROR_SUCCESS) 
                    throw std::system_error(status, std::system_category(), "failed to set scheme default value");

                status = RegSetValueExW(hSchemeKey, L"URL Protocol", 0, REG_SZ, (const BYTE*)L"", sizeof(wchar_t));
                if (status != ERROR_SUCCESS) 
                    throw std::system_error(status, std::system_category(), "failed to set url protocol value");

                HKEY hIconKey;
                status = RegCreateKeyExW(hSchemeKey, L"DefaultIcon", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hIconKey, NULL);
                if (status != ERROR_SUCCESS) 
                    throw std::system_error(status, std::system_category(), "failed to create defaultIcon key");
                RegKeyPtr iconKeyGuard(hIconKey, closeKey);

                status = RegSetValueExW(hIconKey, NULL, 0, REG_SZ, (const BYTE*)wsIconValue.c_str(), (DWORD)((wsIconValue.size() + 1) * sizeof(wchar_t)));
                if (status != ERROR_SUCCESS) 
                    throw std::system_error(status, std::system_category(), "failed to set defaultIcon value");

                HKEY hCommandKey;
                status = RegCreateKeyExW(hSchemeKey, L"shell\\open\\command", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hCommandKey, NULL);
                if (status != ERROR_SUCCESS) 
                    throw std::system_error(status, std::system_category(), "failed to create shell\\open\\command key");
                RegKeyPtr commandKeyGuard(hCommandKey, closeKey);

                status = RegSetValueExW(hCommandKey, NULL, 0, REG_SZ, (const BYTE*)wsCommandValue.c_str(), (DWORD)((wsCommandValue.size() + 1) * sizeof(wchar_t)));
                if (status != ERROR_SUCCESS)
                    throw std::system_error(status, std::system_category(), "failed to set command value");
            }
            catch (const std::exception& e) 
            {
                throw std::runtime_error(std::format("scheme registration for '{}' failed: {}", toString(m_wsScheme), e.what()));
            }
        }

        void unregisterScheme() const 
        {
            const std::wstring wsRegPath = L"Software\\Classes\\" + m_wsScheme;
            LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, wsRegPath.c_str());
            if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
                throw std::runtime_error(std::format("failed to delete registry key. error code: {}", status));
        }

        bool runOrForward(const std::vector<std::wstring>& vecArgs) 
        {
            if (m_pIpc->isServerRunning()) 
            {
                if (!vecArgs.empty())
                    m_pIpc->sendMessage(toString(vecArgs.back()));

                return false;
            }

            m_pIpc->startServer(m_fOnMessage);

            if (!vecArgs.empty() && m_fOnMessage)
            {
                const std::string& sMessage = toString(vecArgs.back());
                if (sMessage.rfind(toString(m_wsScheme) + "://", 0) == 0)
                    m_fOnMessage(sMessage);
            }

            return true;
        }

    private:
        // @todo / SapDragon: fix warning on return WideCharToMultiByte
        static std::string toString(const std::wstring& wsInput) {
            if (wsInput.empty()) return {};
            int iSizeNeeded = WideCharToMultiByte(CP_UTF8, 0, &wsInput[0], (int)wsInput.size(), NULL, 0, NULL, NULL);
            std::string sStrTo(iSizeNeeded, 0);
            WideCharToMultiByte(CP_UTF8, 0, &wsInput[0], (int)wsInput.size(), &sStrTo[0], iSizeNeeded, NULL, NULL);
            return sStrTo;
        }

        std::wstring m_wsScheme;
        std::function<void(const std::string&)> m_fOnMessage;
        std::unique_ptr<ipc::IIpcMechanism> m_pIpc;
    };

} // namespace deeplink