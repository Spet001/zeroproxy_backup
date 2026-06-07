#include "common.hpp"
#include "network_isolation.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include "utils/hook.hpp"
#include "logger/logger.hpp"
#include <string>

#pragma comment(lib, "ws2_32.lib")

namespace network {
    utils::hook::detour connect_hook;
    utils::hook::detour WSAConnect_hook;
    utils::hook::detour getaddrinfo_hook;

    void ForceLocalhost(sockaddr* name, int namelen) {
        if (!name) return;
        if (name->sa_family == AF_INET) {
            sockaddr_in* addr_in = reinterpret_cast<sockaddr_in*>(name);
            uint32_t current_ip = ntohl(addr_in->sin_addr.s_addr);
            // If it's already loopback or local, we could ignore, but to be safe we force 127.0.0.1
            // 0x7F000001 is 127.0.0.1
            addr_in->sin_addr.s_addr = htonl(0x7F000001);
        }
        else if (name->sa_family == AF_INET6) {
            sockaddr_in6* addr_in6 = reinterpret_cast<sockaddr_in6*>(name);
            // Force IPv6 loopback ::1
            const IN6_ADDR loopback = IN6ADDR_LOOPBACK_INIT;
            addr_in6->sin6_addr = loopback;
        }
    }

    int WSAAPI connect_stub(SOCKET s, const sockaddr* name, int namelen) {
        if (name) {
            // Create a copy to modify so we don't write to read-only memory
            char* new_name = (char*)malloc(namelen);
            memcpy(new_name, name, namelen);
            ForceLocalhost(reinterpret_cast<sockaddr*>(new_name), namelen);
            
            auto func = reinterpret_cast<decltype(&connect_stub)>(connect_hook.get_original());
            int result = func(s, reinterpret_cast<const sockaddr*>(new_name), namelen);
            free(new_name);
            return result;
        }
        
        auto func = reinterpret_cast<decltype(&connect_stub)>(connect_hook.get_original());
        return func(s, name, namelen);
    }

    int WSAAPI WSAConnect_stub(SOCKET s, const sockaddr* name, int namelen, LPWSABUF lpCallerData, LPWSABUF lpCalleeData, LPQOS lpSQOS, LPQOS lpGQOS) {
        if (name) {
            char* new_name = (char*)malloc(namelen);
            memcpy(new_name, name, namelen);
            ForceLocalhost(reinterpret_cast<sockaddr*>(new_name), namelen);
            
            auto func = reinterpret_cast<decltype(&WSAConnect_stub)>(WSAConnect_hook.get_original());
            int result = func(s, reinterpret_cast<const sockaddr*>(new_name), namelen, lpCallerData, lpCalleeData, lpSQOS, lpGQOS);
            free(new_name);
            return result;
        }

        auto func = reinterpret_cast<decltype(&WSAConnect_stub)>(WSAConnect_hook.get_original());
        return func(s, name, namelen, lpCallerData, lpCalleeData, lpSQOS, lpGQOS);
    }

    int WSAAPI getaddrinfo_stub(PCSTR pNodeName, PCSTR pServiceName, const ADDRINFOA* pHints, PADDRINFOA* ppResult) {
        // If the game asks for a Demonware domain, we trick it to think it's localhost
        // We just replace the node name with 127.0.0.1
        if (pNodeName) {
            std::string host(pNodeName);
            // Typically "auth.demonware.net" etc, we will just force all lookups to 127.0.0.1 to be safe
            // Or we can check if it contains demonware/activision
            pNodeName = "127.0.0.1"; 
        }
        auto func = reinterpret_cast<decltype(&getaddrinfo_stub)>(getaddrinfo_hook.get_original());
        return func(pNodeName, pServiceName, pHints, ppResult);
    }

    void init_isolation_hooks() {
        HMODULE ws2_32 = GetModuleHandleA("ws2_32.dll");
        if (!ws2_32) {
            ws2_32 = LoadLibraryA("ws2_32.dll");
        }

        if (ws2_32) {
            void* connect_addr = GetProcAddress(ws2_32, "connect");
            if (connect_addr) {
                connect_hook.create(connect_addr, connect_stub);
            }

            void* wsaconnect_addr = GetProcAddress(ws2_32, "WSAConnect");
            if (wsaconnect_addr) {
                WSAConnect_hook.create(wsaconnect_addr, WSAConnect_stub);
            }

            void* getaddrinfo_addr = GetProcAddress(ws2_32, "getaddrinfo");
            if (getaddrinfo_addr) {
                getaddrinfo_hook.create(getaddrinfo_addr, getaddrinfo_stub);
            }
        }
    }
}
