/*
  +--------------------------------------------------------------------------+
  | libcat                                                                   |
  +--------------------------------------------------------------------------+
  | Licensed under the Apache License, Version 2.0 (the "License");          |
  | you may not use this file except in compliance with the License.         |
  | You may obtain a copy of the License at                                  |
  | http://www.apache.org/licenses/LICENSE-2.0                               |
  | Unless required by applicable law or agreed to in writing, software      |
  | distributed under the License is distributed on an "AS IS" BASIS,        |
  | WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. |
  | See the License for the specific language governing permissions and      |
  | limitations under the License. See accompanying LICENSE file.            |
  +--------------------------------------------------------------------------+
  | Author: Twosee <twosee@php.net>                                          |
  +--------------------------------------------------------------------------+
 */

#include "test.h"
#ifdef CAT_CURL

static size_t cat_curl_write_function(const char *ptr, size_t length, size_t n, cat_buffer_t *buffer)
{
    cat_buffer_append(buffer, ptr, length * n);

    return length * n;
}
static void cat_curl_query(const char *url, std::string &response)
{
    CURL *ch;

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));

    ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    curl_easy_setopt(ch, CURLOPT_URL, url);
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, cat_curl_write_function);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, &buffer);
    (void) cat_curl_easy_perform(ch);
    response = std::string(buffer.value, buffer.length);
    cat_buffer_close(&buffer);
    curl_easy_cleanup(ch);
}

TEST(cat_curl, base)
{
    std::string response;
    cat_curl_query(TEST_REMOTE_HTTP_SERVER_HOST, response);
    ASSERT_NE(response.find(TEST_REMOTE_HTTP_SERVER_KEYWORD), std::string::npos);
}

TEST(cat_curl, concurrency)
{
    auto concurrency = ([](uint32_t concurrency) {
        cat_sync_wait_group_t wg;
        ASSERT_NE(cat_sync_wait_group_create(&wg), nullptr);
        ASSERT_TRUE(cat_sync_wait_group_add(&wg, concurrency));
        for (size_t n = 0; n < concurrency; n++) {
            co([&wg] {
                std::string response;
                cat_curl_query(TEST_REMOTE_HTTP_SERVER_HOST, response);
                ASSERT_NE(response.find(TEST_REMOTE_HTTP_SERVER_KEYWORD), std::string::npos);
                ASSERT_TRUE(cat_sync_wait_group_done(&wg));
            });
        }
        ASSERT_TRUE(cat_sync_wait_group_wait(&wg, TEST_IO_TIMEOUT));
    });

    if (!is_valgrind()) {
        auto s1 = cat_time_msec();
        concurrency(TEST_MAX_CONCURRENCY / 4);
        s1 = cat_time_msec() - s1;

        auto s2 = cat_time_msec();
        concurrency(TEST_MAX_CONCURRENCY / 2);
        s2 = cat_time_msec() - s2;

        ASSERT_TRUE((s2 / s1) < 2);
    } else {
        concurrency(TEST_MAX_CONCURRENCY);
    }
}

TEST(cat_curl, cancel)
{
    cat_coroutine_t *coroutine = cat_coroutine_get_current();
    std::string response;

    co([coroutine] {
        cat_time_sleep(0);
        cat_coroutine_resume(coroutine, nullptr, nullptr);
    });
    cat_curl_query(TEST_REMOTE_HTTP_SERVER_HOST, response);
    ASSERT_EQ(response.find(TEST_REMOTE_HTTP_SERVER_KEYWORD), std::string::npos);
}

TEST(cat_curl_multi, base)
{
    CURL *ch;
    CURLM *mh;
    int still_running = 0;
    int repeats = 0;

    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 0));
    DEFER(cat_buffer_close(&buffer));

    ch = curl_easy_init();
    ASSERT_NE(ch, nullptr);
    DEFER(curl_easy_cleanup(ch));
    curl_easy_setopt(ch, CURLOPT_URL, TEST_REMOTE_HTTP_SERVER_HOST);
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, cat_curl_write_function);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, &buffer);
    mh = curl_multi_init();
    ASSERT_NE(mh, nullptr);
    DEFER(curl_multi_cleanup(mh));
    ASSERT_EQ(curl_multi_add_handle(mh, ch), CURLM_OK);
    DEFER(curl_multi_remove_handle(mh, ch));
    ASSERT_EQ(curl_multi_perform(mh, &still_running), CURLM_OK);

    while (still_running) {
        int numfds;
        ASSERT_EQ(cat_curl_multi_wait(mh, NULL, 0, 1000, &numfds), CURLM_OK);
        if (!numfds) {
            repeats++;
            if (repeats > 1) {
                cat_time_msleep(1);
            }
        } else {
            repeats = 0;
        }
        ASSERT_EQ(curl_multi_perform(mh, &still_running), CURLM_OK);
    }

    ASSERT_NE(
        std::string(buffer.value, buffer.length).find(TEST_REMOTE_HTTP_SERVER_KEYWORD),
        std::string::npos
    );
}

#endif