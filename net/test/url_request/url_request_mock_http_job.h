// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A URLRequestJob class that pulls the net and http headers from disk.

#ifndef NET_TEST_URL_REQUEST_URL_REQUEST_MOCK_HTTP_JOB_H_
#define NET_TEST_URL_REQUEST_URL_REQUEST_MOCK_HTTP_JOB_H_

#include <stdint.h>

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "net/url_request/url_request_file_job.h"
#include "url/gurl.h"

namespace base {
class FilePath;
class SequencedWorkerPool;
}

namespace net {
class URLRequestInterceptor;
}

namespace net {

class URLRequestMockHTTPJob : public URLRequestFileJob {
 public:
  // Note that all file IO is done using |worker_pool|.
  URLRequestMockHTTPJob(URLRequest* request,
                        NetworkDelegate* network_delegate,
                        const base::FilePath& file_path,
                        const scoped_refptr<base::TaskRunner>& task_runner);

  // URLRequestJob overrides.
  void Start() override;
  int64_t GetTotalReceivedBytes() const override;
  bool GetMimeType(std::string* mime_type) const override;
  int GetResponseCode() const override;
  bool GetCharset(std::string* charset) override;
  void GetResponseInfo(HttpResponseInfo* info) override;
  bool IsRedirectResponse(GURL* location, int* http_status_code) override;

  // URLRequestFileJob overridess.
  void OnReadComplete(net::IOBuffer* buffer, int result) override;

  // Adds the testing URLs to the URLRequestFilter, both under HTTP and HTTPS.
  static void AddUrlHandlers(
      const base::FilePath& base_path,
      const scoped_refptr<base::SequencedWorkerPool>& worker_pool);

  // Given the path to a file relative to the path passed to AddUrlHandler(),
  // construct a mock URL.
  static GURL GetMockUrl(const std::string& path);
  static GURL GetMockHttpsUrl(const std::string& path);

  // Returns a URLRequestJobFactory::ProtocolHandler that serves
  // URLRequestMockHTTPJob's responding like an HTTP server. |base_path| is the
  // file path leading to the root of the directory to use as the root of the
  // HTTP server.
  static std::unique_ptr<URLRequestInterceptor> CreateInterceptor(
      const base::FilePath& base_path,
      const scoped_refptr<base::SequencedWorkerPool>& worker_pool);

  // Returns a URLRequestJobFactory::ProtocolHandler that serves
  // URLRequestMockHTTPJob's responding like an HTTP server. It responds to all
  // requests with the contents of |file|.
  static std::unique_ptr<URLRequestInterceptor> CreateInterceptorForSingleFile(
      const base::FilePath& file,
      const scoped_refptr<base::SequencedWorkerPool>& worker_pool);

 protected:
  ~URLRequestMockHTTPJob() override;

 private:
  void GetResponseInfoConst(HttpResponseInfo* info) const;
  void SetHeadersAndStart(const std::string& raw_headers);

  std::string raw_headers_;
  int64_t total_received_bytes_ = 0;
  const scoped_refptr<base::TaskRunner> task_runner_;

  base::WeakPtrFactory<URLRequestMockHTTPJob> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(URLRequestMockHTTPJob);
};

}  // namespace net

#endif  // NET_TEST_URL_REQUEST_URL_REQUEST_MOCK_HTTP_JOB_H_
