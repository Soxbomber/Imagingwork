// WinSockFirst.h
// CMakeLists의 /FI 옵션으로 모든 gige 번역 단위의 첫 번째 include로 강제 삽입
// → Qt/windows.h보다 반드시 먼저 winsock2.h가 포함되도록 보장
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCK2API_
#  define _WINSOCK2API_
#endif
#ifndef _WINSOCKAPI_
#  define _WINSOCKAPI_
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
