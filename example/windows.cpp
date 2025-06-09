#include <deeplink.hpp>
#include <iostream>
#include <Windows.h>

void handleDeepLink(const std::string& url) {
    std::cout << "received deep link: " << url << std::endl;
    MessageBoxA(NULL, url.c_str(), "deepink", MB_OK);
}

std::vector<std::wstring> getCommandLineArgs() {
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return {};
    
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    LocalFree(argv);
    return args;
}   



int main() {
    const std::wstring wsScheme = L"myapp";
    
    try {
        deeplink::DeepLink<> handler(wsScheme);
        
        handler.registerScheme();
        
        handler.setOnMessage(handleDeepLink);
        
        std::vector<std::wstring> args = getCommandLineArgs();
        if (!handler.runOrForward(args))
            return 0;
        
        std::cout << "application running. press enter to exit..." << std::endl;
        std::cin.get();

        // handler.unregisterScheme();
    }
    catch (const std::exception& e) {
        MessageBoxA(NULL, e.what(), "error", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    return 0;
}