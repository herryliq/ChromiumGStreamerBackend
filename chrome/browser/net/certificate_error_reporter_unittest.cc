// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/net/certificate_error_reporter.h"

#include <set>
#include <string>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/macros.h"
#include "chrome/browser/net/encrypted_cert_logger.pb.h"
#include "chrome/common/chrome_paths.h"
#include "crypto/curve25519.h"
#include "net/url_request/certificate_report_sender.h"
#include "testing/gtest/include/gtest/gtest.h"

using chrome_browser_net::CertificateErrorReporter;

namespace {

const char kDummyHttpReportUri[] = "http://example.test";
const char kDummyHttpsReportUri[] = "https://example.test";
const char kDummyReport[] = "a dummy report";
const uint32 kServerPublicKeyVersion = 1;

// A mock CertificateReportSender that keeps track of the last report
// sent.
class MockCertificateReportSender : public net::CertificateReportSender {
 public:
  MockCertificateReportSender()
      : net::CertificateReportSender(nullptr, DO_NOT_SEND_COOKIES) {}
  ~MockCertificateReportSender() override {}

  void Send(const GURL& report_uri, const std::string& report) override {
    latest_report_uri_ = report_uri;
    latest_report_ = report;
  }

  const GURL& latest_report_uri() { return latest_report_uri_; }

  const std::string& latest_report() { return latest_report_; }

 private:
  GURL latest_report_uri_;
  std::string latest_report_;

  DISALLOW_COPY_AND_ASSIGN(MockCertificateReportSender);
};

class CertificateErrorReporterTest : public ::testing::Test {
 public:
  CertificateErrorReporterTest() {
    memset(server_private_key_, 1, sizeof(server_private_key_));
    crypto::curve25519::ScalarBaseMult(server_private_key_, server_public_key_);
  }

  ~CertificateErrorReporterTest() override {}

  const uint8_t* server_public_key() { return server_public_key_; }
  const uint8_t* server_private_key() { return server_private_key_; }

 private:
  uint8_t server_public_key_[32];
  uint8_t server_private_key_[32];
};

// Test that CertificateErrorReporter::SendReport sends a plaintext
// report for pinning violation reports.
TEST_F(CertificateErrorReporterTest, PinningViolationSendReport) {
  GURL url(kDummyHttpReportUri);
  MockCertificateReportSender* mock_report_sender =
      new MockCertificateReportSender();
  CertificateErrorReporter reporter(url, server_public_key(),
                                    kServerPublicKeyVersion,
                                    make_scoped_ptr(mock_report_sender));
  reporter.SendReport(CertificateErrorReporter::REPORT_TYPE_PINNING_VIOLATION,
                      kDummyReport);
  EXPECT_EQ(mock_report_sender->latest_report_uri(), url);
  EXPECT_EQ(mock_report_sender->latest_report(), kDummyReport);
}

// Test that CertificateErrorReporter::SendReport sends an encrypted or
// plaintext extended reporting report as appropriate.
TEST_F(CertificateErrorReporterTest, ExtendedReportingSendReport) {
  // Data should not be encrypted when sent to an HTTPS URL.
  MockCertificateReportSender* mock_report_sender =
      new MockCertificateReportSender();
  GURL https_url(kDummyHttpsReportUri);
  CertificateErrorReporter https_reporter(https_url, server_public_key(),
                                          kServerPublicKeyVersion,
                                          make_scoped_ptr(mock_report_sender));
  https_reporter.SendReport(
      CertificateErrorReporter::REPORT_TYPE_EXTENDED_REPORTING, kDummyReport);
  EXPECT_EQ(mock_report_sender->latest_report_uri(), https_url);
  EXPECT_EQ(mock_report_sender->latest_report(), kDummyReport);

  // Data should be encrypted when sent to an HTTP URL.
  if (CertificateErrorReporter::IsHttpUploadUrlSupported()) {
    MockCertificateReportSender* http_mock_report_sender =
        new MockCertificateReportSender();
    GURL http_url(kDummyHttpReportUri);
    CertificateErrorReporter http_reporter(
        http_url, server_public_key(), kServerPublicKeyVersion,
        make_scoped_ptr(http_mock_report_sender));
    http_reporter.SendReport(
        CertificateErrorReporter::REPORT_TYPE_EXTENDED_REPORTING, kDummyReport);

    EXPECT_EQ(http_mock_report_sender->latest_report_uri(), http_url);

    std::string uploaded_report;
#if defined(USE_OPENSSL)
    chrome_browser_net::EncryptedCertLoggerRequest encrypted_request;
    ASSERT_TRUE(encrypted_request.ParseFromString(
        http_mock_report_sender->latest_report()));
    EXPECT_EQ(kServerPublicKeyVersion,
              encrypted_request.server_public_key_version());
    EXPECT_EQ(chrome_browser_net::EncryptedCertLoggerRequest::
                  AEAD_ECDH_AES_128_CTR_HMAC_SHA256,
              encrypted_request.algorithm());
    ASSERT_TRUE(CertificateErrorReporter::DecryptCertificateErrorReport(
        server_private_key(), encrypted_request, &uploaded_report));
#else
    ADD_FAILURE() << "Only supported in OpenSSL ports";
#endif

    EXPECT_EQ(kDummyReport, uploaded_report);
  }
}

#if defined(USE_OPENSSL)
// This test decrypts a "known gold" report. It's intentionally brittle
// in order to catch changes in report encryption that could cause the
// server to no longer be able to decrypt reports that it receives from
// Chrome.
TEST_F(CertificateErrorReporterTest, DecryptExampleReport) {
  // This data should not be changed without also changing the
  // corresponding server-side test.
  const unsigned char kSerializedEncryptedReport[] = {
      0x0A, 0xFB, 0x0C, 0xD5, 0x44, 0x21, 0x36, 0x4D, 0xFC, 0x29, 0x56, 0xBD,
      0x47, 0x18, 0xB1, 0x6F, 0x97, 0xF1, 0xF0, 0x3C, 0x31, 0x31, 0x1D, 0xD7,
      0xAB, 0x81, 0xCC, 0xBC, 0x56, 0x2B, 0xD4, 0x50, 0xF4, 0xF6, 0x28, 0x3A,
      0x36, 0x8C, 0x4A, 0x67, 0x4E, 0xF2, 0x51, 0xA3, 0x7D, 0x02, 0xA8, 0x4D,
      0xE9, 0xBE, 0x72, 0x5A, 0x8A, 0x62, 0xE0, 0x61, 0xA4, 0x87, 0x62, 0xBA,
      0x6A, 0x5C, 0x4B, 0x07, 0x04, 0xE6, 0xCD, 0xE1, 0xD6, 0x12, 0x02, 0xC1,
      0xF3, 0x5C, 0x7D, 0xFB, 0x61, 0xC3, 0x8D, 0xBE, 0x47, 0x50, 0xC4, 0xAC,
      0x33, 0xA6, 0x2B, 0x6A, 0x4D, 0x5F, 0x22, 0x4B, 0x21, 0xAB, 0xFD, 0x66,
      0x9C, 0xEF, 0x81, 0x06, 0xEB, 0xC0, 0x96, 0x87, 0x4E, 0xD6, 0x16, 0x5F,
      0x0B, 0x2E, 0xF0, 0x3C, 0xA3, 0xBF, 0x75, 0x77, 0x3A, 0x91, 0xD2, 0xF5,
      0xCC, 0x22, 0xE2, 0xB0, 0xCC, 0x28, 0xC0, 0xE2, 0xDB, 0x61, 0x5D, 0xEF,
      0x3F, 0xA9, 0x23, 0x71, 0xA1, 0xF3, 0x59, 0x4F, 0xAF, 0xBE, 0x4F, 0x2C,
      0xF6, 0xFC, 0xCB, 0x46, 0x2D, 0x48, 0x24, 0x84, 0xEC, 0x73, 0xCB, 0x83,
      0x3D, 0x2A, 0x0B, 0x9C, 0x57, 0xDC, 0xC5, 0xD9, 0xB9, 0xA2, 0x69, 0xD9,
      0x2B, 0xCF, 0xFB, 0xEB, 0xBA, 0xBC, 0x55, 0x5C, 0xF3, 0x9A, 0x66, 0x56,
      0xD2, 0x06, 0xBF, 0x07, 0x34, 0x7F, 0x84, 0x53, 0xB4, 0xB2, 0xE3, 0x52,
      0xA6, 0x97, 0x2A, 0xFD, 0x43, 0xC8, 0x33, 0xDC, 0x7F, 0xC1, 0x0E, 0xE9,
      0xA6, 0xFF, 0x88, 0x63, 0x07, 0x38, 0x2D, 0xDF, 0x36, 0x83, 0xF1, 0x42,
      0x15, 0x61, 0x05, 0x43, 0x94, 0x59, 0x67, 0x04, 0xB3, 0x8A, 0xF6, 0xFE,
      0x73, 0x03, 0xE2, 0x89, 0x20, 0xC1, 0x63, 0x49, 0x67, 0x4E, 0xAF, 0xBF,
      0xAE, 0xAC, 0xA3, 0x16, 0x8F, 0x6D, 0x2D, 0x79, 0xEA, 0x99, 0x79, 0x95,
      0x03, 0xC8, 0x05, 0x1B, 0x3E, 0x66, 0x99, 0x1E, 0xC5, 0x05, 0x34, 0xD0,
      0x99, 0xED, 0xDD, 0xFB, 0x7C, 0x9B, 0x00, 0x3B, 0x5C, 0x78, 0xD5, 0x30,
      0x76, 0x3C, 0x37, 0xED, 0x4F, 0x6A, 0xAD, 0x75, 0xA7, 0x86, 0xC4, 0x0B,
      0xD5, 0x0F, 0xE8, 0xC3, 0x4D, 0x1F, 0xAF, 0x62, 0xD8, 0xD4, 0x74, 0x02,
      0xBE, 0xD3, 0x01, 0x2F, 0x18, 0x44, 0xFB, 0xA3, 0x46, 0x5B, 0x6F, 0x4C,
      0x86, 0xD9, 0x2D, 0xE3, 0x32, 0x7F, 0xCA, 0x91, 0xFD, 0xED, 0x6A, 0xAC,
      0x1D, 0x01, 0x75, 0xFC, 0x1E, 0x36, 0x81, 0xF3, 0x66, 0x2A, 0x21, 0x0F,
      0x0F, 0x69, 0x29, 0x7B, 0x15, 0xDE, 0xE2, 0x90, 0xF1, 0x64, 0x1F, 0xF3,
      0xEC, 0x90, 0x8A, 0xFC, 0x83, 0x39, 0x7D, 0x19, 0x31, 0x7F, 0x01, 0xA0,
      0x43, 0xF9, 0x24, 0x8C, 0xDD, 0xC0, 0x15, 0x9E, 0x6A, 0x92, 0x8F, 0x65,
      0xDD, 0x60, 0x34, 0x9D, 0x73, 0x46, 0xB5, 0x31, 0xF8, 0x92, 0x79, 0xC3,
      0x59, 0x1D, 0xEB, 0xC8, 0x12, 0x92, 0xB6, 0x24, 0xA2, 0x3A, 0xA1, 0xA2,
      0xEE, 0x8E, 0x34, 0x23, 0xB2, 0x0F, 0x34, 0xA8, 0x29, 0x26, 0x1C, 0xC0,
      0xEE, 0x8C, 0xA7, 0x87, 0x9D, 0x3E, 0x74, 0x21, 0x06, 0xDA, 0xF3, 0x9E,
      0x01, 0xC3, 0xBD, 0x68, 0x40, 0x6B, 0x61, 0xA9, 0xB7, 0xC1, 0xFD, 0x56,
      0xFF, 0x99, 0x11, 0x42, 0x81, 0xFB, 0xE0, 0x9A, 0x1D, 0xD9, 0xB8, 0x1D,
      0x2D, 0x85, 0x74, 0xC1, 0xBC, 0x36, 0x8F, 0x31, 0xE5, 0x01, 0x79, 0xF5,
      0x04, 0xC5, 0x96, 0x1B, 0x5F, 0xAD, 0x86, 0x52, 0x00, 0xF0, 0xCC, 0x7B,
      0x8D, 0x1B, 0xEA, 0x6B, 0xA8, 0xF8, 0xA4, 0x8F, 0x13, 0x51, 0x3D, 0xB8,
      0x4D, 0x99, 0x22, 0x9B, 0x31, 0xB7, 0xBC, 0xF7, 0x2D, 0x76, 0x19, 0x90,
      0xAB, 0xDA, 0xD2, 0x25, 0xE7, 0x4E, 0xDF, 0x96, 0x66, 0x90, 0xD7, 0x4E,
      0xE7, 0x21, 0x96, 0xEF, 0xD0, 0xA7, 0x00, 0x2E, 0x9B, 0x2C, 0xE3, 0x87,
      0x45, 0xA4, 0x3C, 0x24, 0x5A, 0xFA, 0x3D, 0x2D, 0xAD, 0x3E, 0xD3, 0xB5,
      0x07, 0xAB, 0x72, 0x6D, 0xD1, 0x83, 0x17, 0x11, 0xD8, 0x37, 0x7D, 0x69,
      0xE1, 0x4D, 0xF6, 0x34, 0x72, 0x54, 0xCD, 0x65, 0xC0, 0x2C, 0x36, 0xA1,
      0x0A, 0x4B, 0x28, 0x24, 0x50, 0x1D, 0x36, 0x15, 0xF3, 0xD4, 0xFB, 0x75,
      0x2C, 0x72, 0xA9, 0x92, 0x34, 0xB5, 0xEF, 0x50, 0x29, 0x8D, 0x78, 0x75,
      0xB8, 0x19, 0x58, 0xC2, 0x9D, 0xD3, 0x09, 0xDC, 0xB6, 0xB6, 0x86, 0xE8,
      0xF7, 0x79, 0xBF, 0xFB, 0x7E, 0xF4, 0xD5, 0x99, 0xFF, 0xE5, 0x72, 0x1A,
      0x15, 0x9E, 0x37, 0x6A, 0x7A, 0xD1, 0xD3, 0x3D, 0xC8, 0xDC, 0x37, 0x98,
      0xE4, 0x74, 0x0B, 0x8D, 0x9D, 0x38, 0x7E, 0xA8, 0x24, 0x76, 0xA4, 0x7F,
      0x28, 0x34, 0xA9, 0xC5, 0x5F, 0xD2, 0x0C, 0xDE, 0xD0, 0x34, 0x2D, 0xF9,
      0x25, 0xE0, 0x60, 0xB2, 0x1D, 0xA8, 0x7F, 0xDB, 0x03, 0x44, 0x88, 0xA2,
      0x33, 0x75, 0x9B, 0x06, 0xAB, 0x28, 0x82, 0x74, 0x9F, 0x7F, 0xA7, 0xA6,
      0x38, 0x27, 0xFA, 0xCE, 0x75, 0xC8, 0x91, 0xE1, 0x15, 0xDD, 0x2F, 0x34,
      0xF5, 0x64, 0xFA, 0x77, 0x6D, 0x1F, 0xE7, 0x42, 0x41, 0xB5, 0xF4, 0x71,
      0x8E, 0x0A, 0x8B, 0x06, 0x00, 0xB6, 0xCB, 0xBE, 0x23, 0xC2, 0x8C, 0x83,
      0x27, 0x23, 0x8F, 0xF7, 0xA1, 0xCF, 0x5C, 0x76, 0x16, 0x9C, 0x17, 0xD1,
      0x7D, 0xA5, 0xA0, 0x55, 0xC2, 0xF7, 0x5B, 0x8B, 0x7E, 0xD7, 0x36, 0xC0,
      0x3B, 0x52, 0xF4, 0x5D, 0x96, 0x99, 0x61, 0x16, 0xFF, 0x01, 0x1D, 0x2F,
      0xC4, 0xE6, 0x3D, 0x6E, 0x1F, 0xB3, 0x2B, 0x4B, 0x9E, 0xC4, 0xD8, 0x7F,
      0x74, 0x6B, 0x5F, 0x78, 0x36, 0xE6, 0x2E, 0x46, 0xF1, 0xCF, 0x9E, 0x19,
      0xA3, 0xE1, 0x5C, 0xC8, 0x4F, 0xE5, 0x36, 0x21, 0x06, 0x1A, 0x9D, 0x7D,
      0x14, 0xBE, 0xCB, 0x1F, 0xB7, 0x8E, 0xC4, 0x98, 0xEA, 0xDC, 0xEC, 0x59,
      0xA1, 0xC6, 0x77, 0xCF, 0x2D, 0x47, 0x69, 0x29, 0x8C, 0xC3, 0xBF, 0x72,
      0xA3, 0x3F, 0x40, 0xFB, 0x11, 0xDA, 0x0C, 0x0B, 0xB4, 0x66, 0xD3, 0xDD,
      0x12, 0x7B, 0xB1, 0x6A, 0xC3, 0xF6, 0x5F, 0x0F, 0xFB, 0x6D, 0x43, 0x6B,
      0xED, 0xF9, 0x48, 0x4E, 0xAF, 0x98, 0x55, 0x1B, 0x37, 0x16, 0x2D, 0xF3,
      0x75, 0xB5, 0xAC, 0xB8, 0xF1, 0x37, 0xE8, 0xA9, 0x99, 0x35, 0x04, 0x8E,
      0x51, 0x7B, 0x29, 0x4B, 0x7A, 0xA1, 0xD2, 0x1D, 0x25, 0x62, 0xFD, 0xAF,
      0x7A, 0xBA, 0xB6, 0x05, 0x75, 0x5D, 0x94, 0x72, 0xE7, 0x02, 0x77, 0x02,
      0xAC, 0x7B, 0x91, 0x6F, 0x8C, 0x32, 0xF6, 0x38, 0x67, 0xF6, 0xF2, 0xC1,
      0x58, 0xCE, 0x01, 0x39, 0xED, 0x76, 0x52, 0x1F, 0xA2, 0x49, 0x0B, 0x72,
      0x73, 0xD9, 0x00, 0x12, 0xDC, 0xC5, 0x27, 0x8F, 0x38, 0x08, 0x31, 0x7F,
      0x08, 0xFC, 0xA8, 0x74, 0xD2, 0xED, 0xED, 0xC7, 0x37, 0xC8, 0xAF, 0xB1,
      0x2C, 0x9D, 0x33, 0x2C, 0xE1, 0x2D, 0x72, 0x59, 0xCF, 0x55, 0x1E, 0x04,
      0x51, 0x08, 0xBF, 0x98, 0x16, 0xEC, 0x1F, 0x76, 0x54, 0x5F, 0x8B, 0xD1,
      0x00, 0x07, 0x25, 0x7A, 0x0A, 0x2A, 0xD1, 0xAE, 0xC8, 0x77, 0xDF, 0xDD,
      0x14, 0xB2, 0xA6, 0xC5, 0x2E, 0xFB, 0xC7, 0x4E, 0x56, 0x01, 0xDE, 0x5B,
      0x86, 0xAC, 0xB7, 0xBB, 0x6E, 0x41, 0xFF, 0xFD, 0x29, 0x29, 0x0D, 0x95,
      0x13, 0x1E, 0x31, 0xA0, 0xFF, 0xC2, 0x2C, 0x31, 0x6B, 0xF5, 0x0D, 0x16,
      0x1E, 0x56, 0xC5, 0x1F, 0xB1, 0xB1, 0x33, 0x3D, 0xA9, 0xD4, 0x8D, 0x2A,
      0xFA, 0x9F, 0x9A, 0xA7, 0x51, 0xDC, 0x77, 0xBB, 0xD6, 0xDC, 0xAE, 0x3D,
      0xA3, 0x2F, 0xDD, 0x55, 0x52, 0xAB, 0x35, 0x61, 0x7C, 0xA8, 0x5E, 0x57,
      0xAD, 0x8D, 0xF5, 0x02, 0xA1, 0x60, 0x33, 0x9E, 0xEC, 0xD0, 0x48, 0x5C,
      0x3F, 0xDF, 0xF2, 0x33, 0xC1, 0x3A, 0x99, 0xFE, 0x37, 0x2E, 0xF8, 0xFF,
      0x49, 0x11, 0xFF, 0x8B, 0x18, 0xCF, 0x37, 0xBC, 0x50, 0xD0, 0xFB, 0x9E,
      0xFB, 0x16, 0x6D, 0xC6, 0xAC, 0x79, 0xDD, 0xE8, 0xE7, 0x69, 0x62, 0xB7,
      0x23, 0xDF, 0xA6, 0x93, 0x6E, 0x65, 0x49, 0xE5, 0x61, 0x60, 0x89, 0xDC,
      0x45, 0xC8, 0xD2, 0x4F, 0x03, 0xAA, 0x1E, 0x06, 0x19, 0x4B, 0x14, 0x12,
      0x02, 0xB9, 0xA2, 0x66, 0x02, 0xFE, 0x80, 0x03, 0xC7, 0xEF, 0x3C, 0xC9,
      0x0D, 0x85, 0xD8, 0x94, 0xF2, 0x3B, 0xC6, 0x9E, 0xB7, 0x4D, 0x19, 0x85,
      0x1A, 0xA6, 0x89, 0x12, 0x24, 0xC2, 0x16, 0x3A, 0x17, 0x1E, 0x64, 0x32,
      0x6D, 0xDA, 0x6B, 0xE0, 0x3C, 0xE9, 0xCC, 0xE1, 0xFC, 0x16, 0x9B, 0xBF,
      0x75, 0x01, 0xA4, 0x17, 0x5F, 0x49, 0xD3, 0xF7, 0xE3, 0xEF, 0x1B, 0x4D,
      0x90, 0xB1, 0x43, 0x54, 0x97, 0x57, 0xE3, 0x4B, 0x66, 0x77, 0xAA, 0x1C,
      0xA4, 0xC1, 0x6C, 0x44, 0x34, 0x93, 0x42, 0xDD, 0xC6, 0xA2, 0xBD, 0x95,
      0x84, 0x1C, 0xB5, 0xE0, 0xEC, 0x24, 0x6E, 0x64, 0x5C, 0x94, 0x81, 0x50,
      0x7E, 0x97, 0x16, 0xA8, 0x7C, 0xF3, 0x6D, 0x5A, 0x7C, 0x55, 0x71, 0x2D,
      0x3D, 0x8A, 0xCD, 0xA2, 0x9B, 0x04, 0x10, 0xEE, 0xE2, 0x2C, 0x4D, 0x50,
      0x93, 0x1F, 0xD2, 0x36, 0x05, 0x25, 0x21, 0xA6, 0x69, 0x99, 0xC8, 0x76,
      0x1B, 0x01, 0xDE, 0x9F, 0xEE, 0xE0, 0xFF, 0xAF, 0x3C, 0x0F, 0x0D, 0xF1,
      0x49, 0x83, 0x17, 0x1A, 0x88, 0x31, 0xC6, 0x10, 0xFB, 0x5C, 0xBC, 0xC7,
      0x8F, 0x71, 0x37, 0x17, 0xA7, 0xF0, 0xDE, 0x1A, 0x89, 0xBB, 0x62, 0x28,
      0x07, 0xFF, 0xB2, 0xFA, 0x6F, 0x91, 0x30, 0xEC, 0x90, 0x84, 0xF6, 0xE3,
      0xA7, 0x78, 0x81, 0x13, 0x6C, 0xC7, 0x1F, 0x57, 0xB1, 0x27, 0x4F, 0x43,
      0xAB, 0x58, 0x92, 0x48, 0xCD, 0x94, 0x7B, 0xEA, 0xEC, 0x1F, 0xE6, 0x65,
      0x3E, 0xD8, 0x14, 0x1B, 0x96, 0x09, 0xD1, 0x05, 0xCC, 0xDD, 0xB7, 0xBC,
      0x69, 0xF5, 0x33, 0x58, 0x0C, 0x32, 0x27, 0xA2, 0xF5, 0xE4, 0x28, 0x1C,
      0xD2, 0xC0, 0xF8, 0x67, 0xD4, 0x58, 0xC4, 0xA4, 0x12, 0x30, 0x0E, 0x4D,
      0xD7, 0x7E, 0x2B, 0x01, 0xC5, 0xD3, 0xA7, 0xF9, 0xEA, 0xFE, 0x3D, 0x04,
      0x9E, 0xE8, 0x39, 0x9E, 0xC7, 0xE5, 0xF1, 0xCC, 0x72, 0xB7, 0x5E, 0x5B,
      0xFC, 0xAB, 0xF4, 0x42, 0x3E, 0x7A, 0xBE, 0x8C, 0x03, 0xB1, 0x11, 0x4E,
      0x19, 0xFE, 0xB2, 0xFD, 0xF9, 0x9A, 0xE0, 0xC7, 0x1A, 0xCA, 0xFF, 0xD7,
      0x31, 0x40, 0x43, 0x2A, 0xD1, 0x1D, 0xF7, 0x5A, 0x9A, 0x3B, 0xB9, 0x3C,
      0x12, 0x48, 0xB3, 0x7D, 0xC3, 0xE7, 0x64, 0x97, 0x55, 0x5E, 0x70, 0x9B,
      0x75, 0xD6, 0xC5, 0x73, 0x4E, 0xFA, 0xB1, 0x2F, 0xDF, 0x3F, 0x8E, 0x97,
      0xA6, 0x67, 0xFE, 0x4D, 0x3F, 0x5C, 0x09, 0x9B, 0x98, 0xBA, 0xF8, 0xA5,
      0x6D, 0x18, 0x80, 0x61, 0xE9, 0x17, 0x4A, 0xDD, 0x95, 0x92, 0x4F, 0xD4,
      0x57, 0xD0, 0x40, 0xE0, 0x21, 0xC4, 0x76, 0xE2, 0x1A, 0x1E, 0x1F, 0x29,
      0x0A, 0x98, 0xC9, 0x93, 0xA8, 0x6A, 0x55, 0x26, 0x67, 0xAA, 0x14, 0x18,
      0x6A, 0x38, 0x91, 0xEB, 0x13, 0xD0, 0xA8, 0x00, 0x4B, 0x13, 0xB7, 0x3B,
      0x13, 0x74, 0x34, 0xB1, 0xEA, 0x1F, 0x59, 0x4C, 0x1C, 0x7F, 0x73, 0xE8,
      0xF0, 0xE1, 0x10, 0x23, 0xA2, 0x77, 0x35, 0xCA, 0x57, 0x9F, 0x43, 0xE7,
      0xCA, 0xF2, 0xD2, 0xB1, 0x38, 0x27, 0x4D, 0x52, 0xEE, 0x82, 0xB6, 0x3E,
      0xF4, 0xB0, 0x51, 0x82, 0x9E, 0xDB, 0xB9, 0xAE, 0xCC, 0xFD, 0x97, 0x60,
      0x1E, 0x67, 0x5B, 0x5A, 0x6A, 0x9A, 0xEA, 0x0F, 0x90, 0x36, 0xA1, 0xD0,
      0x7D, 0x5E, 0xC3, 0x90, 0x3D, 0x7E, 0xD4, 0xEF, 0xCF, 0xD2, 0x38, 0x67,
      0xBB, 0x1F, 0x58, 0xD4, 0x1B, 0xF2, 0xF0, 0x6A, 0x25, 0x68, 0x82, 0x19,
      0x78, 0xB2, 0xC1, 0x34, 0x1D, 0xFA, 0xD5, 0x24, 0x1C, 0x81, 0x21, 0x74,
      0xB1, 0xE8, 0x59, 0xD7, 0xA1, 0xB7, 0x61, 0xF5, 0x4F, 0x41, 0xEC, 0x27,
      0xE5, 0x30, 0xC2, 0xFB, 0x69, 0xCC, 0x69, 0xF5, 0x0F, 0xF2, 0x0D, 0x2E,
      0xDE, 0x43, 0xC5, 0xA2, 0xA6, 0x99, 0x1F, 0x00, 0x06, 0xD7, 0x93, 0xA5,
      0xD7, 0xAD, 0xD0, 0x6A, 0x61, 0x37, 0xF4, 0xAA, 0xB8, 0xA9, 0x3B, 0x7E,
      0xFC, 0xF7, 0x30, 0xE6, 0xA8, 0x75, 0x65, 0xBA, 0xDD, 0x1D, 0x30, 0x73,
      0x04, 0x5A, 0x37, 0x64, 0xE3, 0x51, 0xFD, 0x36, 0x4D, 0xF1, 0x8F, 0x7E,
      0x38, 0x18, 0xA0, 0x43, 0xE3, 0x9F, 0x03, 0x70, 0x53, 0x08, 0xF1, 0xE1,
      0x13, 0x84, 0x7C, 0x5F, 0x1F, 0xDE, 0x10, 0x01, 0x1A, 0x20, 0xCC, 0x49,
      0xFB, 0xD4, 0xE1, 0x04, 0x42, 0x0D, 0x2C, 0x41, 0x84, 0xDD, 0xFB, 0xC7,
      0xA6, 0x2D, 0x00, 0xCC, 0xB5, 0x3B, 0x31, 0x2E, 0xB4, 0x30, 0xA5, 0x08,
      0x1A, 0x7D, 0x19, 0x81, 0xF0, 0x4D, 0x20, 0x01};

  chrome_browser_net::EncryptedCertLoggerRequest encrypted_request;
  std::string decrypted_serialized_report;
  ASSERT_TRUE(encrypted_request.ParseFromString(
      std::string(reinterpret_cast<const char*>(kSerializedEncryptedReport),
                  sizeof(kSerializedEncryptedReport))));
  ASSERT_TRUE(
      chrome_browser_net::CertificateErrorReporter::
          DecryptCertificateErrorReport(server_private_key(), encrypted_request,
                                        &decrypted_serialized_report));
}
#endif

}  // namespace
