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

int tlsb_http_get(const char *url, FILE *f, int *ret_len, int timeout)
{
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    BOOL  bResults = FALSE;
    HINTERNET  hSession = NULL,
               hConnect = NULL,
               hRequest = NULL;

    int result = 0;
    if (ret_len)
        *ret_len = 0;

    int url_len = strlen(url);
    WCHAR *url_wc = alloca((url_len + 1) * sizeof(WCHAR));
    WCHAR *host_wc = alloca((url_len + 1) * sizeof(WCHAR));
    WCHAR *path_wc = alloca((url_len + 1) * sizeof(WCHAR));

    mbstowcs_s(NULL, url_wc, url_len + 1, url, _TRUNCATE);

    URL_COMPONENTS urlComp;
    memset(&urlComp, 0, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);

    urlComp.lpszHostName = host_wc;
    urlComp.dwHostNameLength  = (DWORD)(url_len + 1);

    urlComp.lpszUrlPath = path_wc;
    urlComp.dwUrlPathLength   = (DWORD)(url_len + 1);

    // Crack the url_wc.
    if (!WinHttpCrackUrl(url_wc, 0, 0, &urlComp)) {
        log_msg("Error %u in WinHttpCrackUrl.", GetLastError());
        goto error_out;
    }

    char buffer[16 * 1024];

    // Use WinHttpOpen to obtain a session handle.
    hSession = WinHttpOpen( L"toliss_sb",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0 );

    if (NULL == hSession) {
        log_msg("Can't open HTTP session");
        goto error_out;
    }

    timeout *= 1000;
    if (! WinHttpSetTimeouts(hSession, timeout, timeout, timeout, timeout)) {
        log_msg("can't set timeouts");
        goto error_out;
    }

    hConnect = WinHttpConnect(hSession, host_wc, urlComp.nPort, 0);
    if (NULL == hConnect) {
        log_msg("Can't open HTTP session");
        goto error_out;
    }

    hRequest = WinHttpOpenRequest(hConnect, L"GET", path_wc, NULL, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
    if (NULL == hRequest) {
        log_msg("Can't open HTTP request: %u", GetLastError());
        goto error_out;
    }

    bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (! bResults) {
        log_msg("Can't send HTTP request: %u", GetLastError());
        goto error_out;
    }

    bResults = WinHttpReceiveResponse(hRequest, NULL);
    if (! bResults) {
        log_msg("Can't receive response", GetLastError());
        goto error_out;
    }

    while (1) {
        DWORD res = WinHttpQueryDataAvailable(hRequest, &dwSize);
        if (!res) {
            log_msg("%d, Error %u in WinHttpQueryDataAvailable.", res, GetLastError());
            goto error_out;
        }

        // log_msg("dwSize %d", dwSize);
        if (0 == dwSize) {
            break;
        }

        while (dwSize > 0) {
            int get_len = (dwSize < sizeof(buffer) ? dwSize : sizeof(buffer));

            bResults = WinHttpReadData(hRequest, buffer, get_len, &dwDownloaded);
            if (! bResults){
               log_msg("Error %u in WinHttpReadData.", GetLastError());
               goto error_out;
            }

            if (NULL != f) {
                fwrite(buffer, 1, dwDownloaded, f);
                if (ferror(f)) {
                    log_msg("error wrinting file");
                    goto error_out;
                }
            }

            dwSize -= dwDownloaded;
            if (ret_len)
                *ret_len += dwDownloaded;
        }
    }

    result = 1;

error_out:
    // Close any open handles.
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);

    log_msg("tlsb_http_get result: %d", result);
    return result;
}
