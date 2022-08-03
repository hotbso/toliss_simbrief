/*
MIT License

Copyright (c) 2019 Holger Teutsch / Bajan002

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
#include <curl/curl.h>

#include "tlsb.h"


static size_t discard_write_cb(const void *ptr, size_t size, size_t nmemb, FILE *userdata)
{
    return size * nmemb;
}

int tlsb_http_get(const char *url, FILE *f, int *ret_len, int timeout)
{
  CURL *curl;
  CURLcode res;
  curl_global_init(CURL_GLOBAL_ALL);
  curl = curl_easy_init();
  if(!curl) return 0;
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, (NULL != f) ? fwrite : discard_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);

  curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  res = curl_easy_perform(curl);
    /* Check for errors */
  if(res != CURLE_OK) {
      log_msg("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
      return 0;
  }
  double dl;
  res = curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &dl);
  if(res == CURLE_OK && ret_len) *ret_len = (int)dl;
  curl_easy_cleanup(curl);
  curl_global_cleanup();
  return 1;
}
