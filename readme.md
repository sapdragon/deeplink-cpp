# deeplink-cpp

Header-only C++ library for registering and handling custom URL schemes on Windows.

## Usage

```cpp
#include <deeplink.hpp>
#include <iostream>
#include <Windows.h>

void handleDeepLink(const std::string& url) {
    std::cout << "received deep link: " << url << std::endl;
    MessageBoxA(NULL, url.c_str(), "deep    ink", MB_OK);
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
```

Test with: `myapp://test-data` in browser or command line.

## API

### DeepLink
- `DeepLink(std::wstring wsScheme)` - Constructor
- `registerScheme()` - Register URL scheme in Windows registry
- `unregisterScheme()` - Remove URL scheme from registry
- `setOnMessage(callback)` - Set URL handler function
- `runOrForward(args)` - Handle single instance logic, returns false if forwarded

## How it works

1. Registers custom URL scheme in Windows registry
2. Uses Named Pipes for inter-process communication
3. Forwards URLs to existing instance or starts new server
4. Calls your callback function with received URLs

## Requirements

- Windows 7+
- C++20 compiler
