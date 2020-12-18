#pragma once

#include <iostream>
#include <fstream>

namespace common {

/*
   The operating system, must be one of: (I_OS_x)
     DARWIN   - Any Darwin system (macOS, iOS, watchOS, tvOS)
     ANDROID  - Android platform
     WIN32    - Win32 (Windows 2000/XP/Vista/7 and Windows Server 2003/2008)
     WINRT    - WinRT (Windows Runtime)
     CYGWIN   - Cygwin
     LINUX    - Linux
     FREEBSD  - FreeBSD
     OPENBSD  - OpenBSD
     SOLARIS  - Sun Solaris
     AIX      - AIX
     UNIX     - Any UNIX BSD/SYSV system
*/

#define OS_PLATFORM_UTIL_VERSION 1.0.0.180723

// DARWIN
#if defined(__APPLE__) && (defined(__GNUC__) || defined(__xlC__) || defined(__xlc__))
    #include <TargetConditionals.h>
    #if defined(TARGET_OS_MAC) && TARGET_OS_MAC
        #define I_OS_DARWIN
        #ifdef __LP64__
            #define I_OS_DARWIN64
        #else
            #define I_OS_DARWIN32
        #endif
    #else
        #error "not support this Apple platform"
    #endif
// ANDROID
#elif defined(__ANDROID__) || defined(ANDROID)
    #define I_OS_ANDROID
    #define I_OS_LINUX
// Windows
#elif !defined(SAG_COM) && (!defined(WINAPI_FAMILY) || WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP) \
    && (defined(WIN64) || defined(_WIN64) || defined(__WIN64__))
    #define I_OS_WIN32
    #define I_OS_WIN64
#elif !defined(SAG_COM) && (defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__))
    #if defined(WINAPI_FAMILY)
        #ifndef WINAPI_FAMILY_PC_APP
            #define WINAPI_FAMILY_PC_APP WINAPI_FAMILY_APP
        #endif
        #if defined(WINAPI_FAMILY_PHONE_APP) && WINAPI_FAMILY == WINAPI_FAMILY_PHONE_APP
            #define I_OS_WINRT
        #elif WINAPI_FAMILY == WINAPI_FAMILY_PC_APP
            #define I_OS_WINRT
        #else
            #define I_OS_WIN32
        #endif
    #else
        #define I_OS_WIN32
    #endif
// CYGWIN
#elif defined(__CYGWIN__)
    #define I_OS_CYGWIN
// sun os
#elif defined(__sun) || defined(sun)
    #define I_OS_SOLARIS
// LINUX
#elif defined(__linux__) || defined(__linux)
    #define I_OS_LINUX
// FREEBSD
#elif defined(__FreeBSD__) || defined(__DragonFly__) || defined(__FreeBSD_kernel__)
    #ifndef __FreeBSD_kernel__
        #define I_OS_FREEBSD
    #endif
    #define I_OS_FREEBSD_KERNEL
// OPENBSD
#elif defined(__OpenBSD__)
    #define I_OS_OPENBSD
// IBM AIX
#elif defined(_AIX)
    #define I_OS_AIX
#else
    #error "not support this OS"
#endif

#if defined(I_OS_WIN32) || defined(I_OS_WIN64) || defined(I_OS_WINRT)
    #define I_OS_WIN
#endif

#if defined(I_OS_WIN)
    #undef I_OS_UNIX
#elif !defined(I_OS_UNIX)
    #define I_OS_UNIX
#endif

#ifdef I_OS_DARWIN
    #define I_OS_MAC
#endif
#ifdef I_OS_DARWIN32
    #define I_OS_MAC32
#endif
#ifdef I_OS_DARWIN64
    #define I_OS_MAC64
#endif

#ifdef I_OS_WIN

    #include <windows.h>
    #include <tchar.h>
    #include <strsafe.h>

    #pragma comment(lib, "advapi32.lib")

void SvcInstall(const std::string &svcName) {
    SC_HANDLE schSCManager;
    SC_HANDLE schService;
    TCHAR szPath[MAX_PATH];

    if (!GetModuleFileName(NULL, szPath, MAX_PATH)) {
        printf("Cannot install service (%ld)\n", GetLastError());
        return;
    }

    // Get a handle to the SCM database.

    schSCManager = OpenSCManager(NULL,                   // local computer
                                 NULL,                   // ServicesActive database
                                 SC_MANAGER_ALL_ACCESS); // full access rights

    if (NULL == schSCManager) {
        printf("OpenSCManager failed (%ld)\n", GetLastError());
        return;
    }

    // Create the service

    schService = CreateService(schSCManager,              // SCM database
                               TEXT(svcName.c_str()),     // name of service
                               TEXT(svcName.c_str()),     // service name to display
                               SERVICE_ALL_ACCESS,        // desired access
                               SERVICE_WIN32_OWN_PROCESS, // service type
                               SERVICE_AUTO_START,        // start type
                               SERVICE_ERROR_NORMAL,      // error control type
                               szPath,                    // path to service's binary
                               NULL,                      // no load ordering group
                               NULL,                      // no tag identifier
                               NULL,                      // no dependencies
                               NULL,                      // LocalSystem account
                               NULL);                     // no password

    if (schService == NULL) {
        printf("CreateService failed (%ld)\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return;
    } else {
        printf("Service installed successfully\n");
    }

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

#endif

#ifdef I_OS_UNIX

    #include <fstream>
    #include <cstdlib>
void SvcInstall(const std::string &svcName) {
    std::ofstream ofs("/lib/systemd/system/" + svcName + ".service");
    ofs << "[Unit]" << std::endl
        << "After=network.target" << std::endl
        << "[Service]" << std::endl
        << "ExecStart=" << svcName << std::endl
        << "ExecReload=/bin/kill -HUP $MAINPID" << std::endl
        << "Type=simple" << std::endl
        << "KillMode=control-group" << std::endl
        << "[Install]" << std::endl
        << "WantedBy=multi-user.target" << std::endl;
    ofs.close();
    system("chmod 754 /lib/systemd/system/" + svcName + ".service");
    system("systemctl daemon-reload");
    system("systemctl enable " + svcName + ".service");
}

#endif

} // namespace common
