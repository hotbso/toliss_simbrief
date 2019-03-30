/*
MIT License

Copyright (c) 2019 Holger Teutsch

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdlib.h>
#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <WinHttp.h>

#include "tlsb.h"

int tlsb_ofp_get(const char *userid, char *buffer, int buflen, int *retlen)
{
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    BOOL  bResults = FALSE;
    HINTERNET  hSession = NULL,
               hConnect = NULL,
               hRequest = NULL;

    // Use WinHttpOpen to obtain a session handle.
    hSession = WinHttpOpen( L"toliss_sb",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0 );

    int result = 0;

    if (NULL == hSession) {
        log_msg("Can't open HTTP session");
        goto error_out;
    }

    hConnect = WinHttpConnect(hSession, L"www.simbrief.com", INTERNET_DEFAULT_HTTPS_PORT, 0);

    if (NULL == hConnect) {
        log_msg("Can't open HTTP session");
        goto error_out;
    }

    snprintf(buffer, buflen, "/api/xml.fetcher.php?userid=%s", userid);
    log_msg(buffer);

    wchar_t object[200];
    size_t converted;
    mbstowcs_s(&converted, object, 200, buffer, _TRUNCATE);
    hRequest = WinHttpOpenRequest(hConnect, L"GET", object, NULL, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (NULL == hRequest) {
        log_msg("Can't open HTTP request");
        goto error_out;
    }

    bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

    if (! bResults) {
        log_msg("Can't send HTTP request");
        goto error_out;
    }

    bResults = WinHttpReceiveResponse(hRequest, NULL);
    if (! bResults) {
        log_msg("Can't receive response");
        goto error_out;
    }

    int bofs = 0;

    while (1) {
        DWORD res = WinHttpQueryDataAvailable(hRequest, &dwSize);
        if (!res) {
            log_msg("%d, Error %u in WinHttpQueryDataAvailable.", res, GetLastError());
            goto error_out;
        }

        //log_msg("dwSize %d", dwSize);
        if (0 == dwSize) {
            *retlen = bofs;
            break;
        }

        if (dwSize > buflen) {
            log_msg("Buffer to small");
            goto error_out;
        }

        bResults = WinHttpReadData(hRequest, buffer + bofs, dwSize, &dwDownloaded);
        if (! bResults){
           log_msg("Error %u in WinHttpReadData.", GetLastError());
           goto error_out;
        }

        buflen -= dwDownloaded;
        bofs += dwDownloaded;
    }

    result = 1;

error_out:
    // Close any open handles.
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    return result;
}
