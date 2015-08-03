// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A binary wrapper for QuicClient.
// Connects to a host using QUIC, sends a request to the provided URL, and
// displays the response.
//
// Some usage examples:
//
//   TODO(rtenneti): make --host optional by getting IP Address of URL's host.
//
//   Get IP address of the www.google.com
//   IP=`dig www.google.com +short | head -1`
//
// Standard request/response:
//   quic_client http://www.google.com  --host=${IP}
//   quic_client http://www.google.com --quiet  --host=${IP}
//   quic_client https://www.google.com --port=443  --host=${IP}
//
// Use a specific version:
//   quic_client http://www.google.com --version=23  --host=${IP}
//
// Send a POST instead of a GET:
//   quic_client http://www.google.com --body="this is a POST body" --host=${IP}
//
// Append additional headers to the request:
//   quic_client http://www.google.com  --host=${IP}
//               --headers="Header-A: 1234; Header-B: 5678"
//
// Connect to a host different to the URL being requested:
//   Get IP address of the www.google.com
//   IP=`dig www.google.com +short | head -1`
//   quic_client mail.google.com --host=${IP}
//
// Try to connect to a host which does not speak QUIC:
//   Get IP address of the www.example.com
//   IP=`dig www.example.com +short | head -1`
//   quic_client http://www.example.com --host=${IP}

#include <iostream>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/base/privacy_mode.h"
//#include "net/cert/cert_verifier.h"
//#include "net/http/transport_security_state.h"
//#include "net/log/net_log.h"
//#include "net/quic/crypto/proof_verifier_chromium.h"
#include "net/quic/quic_protocol.h"
#include "net/quic/quic_server_id.h"
#include "net/quic/quic_utils.h"
#include "net/quic/quic_config.h"
#include "net/tools/epoll_server/epoll_server.h"
#include "net/tools/quic/quic_client.h"
//#include "net/tools/quic/spdy_balsa_utils.h"
//#include "net/tools/quic/synchronous_host_resolver.h"
//#include "url/gurl.h"

using base::StringPiece;
//using net::CertVerifier;
//using net::ProofVerifierChromium;
//using net::TransportSecurityState;
using std::cout;
using std::cerr;
using std::map;
using std::string;
using std::vector;
using std::endl;

// The IP or hostname the quic client will connect to.
string FLAGS_host = "127.0.0.1";
// The port to connect to.
int32 FLAGS_port = 80;
// If set, send a POST with this body.
string FLAGS_body = "";
// A semicolon separated list of key:value pairs to add to request headers.
string FLAGS_headers = "";
// Set to true for a quieter output experience.
bool FLAGS_quiet = false;
// QUIC version to speak, e.g. 21. If not set, then all available versions are
// offered in the handshake.
int32 FLAGS_quic_version = -1;
// If true, a version mismatch in the handshake is not considered a failure.
// Useful for probing a server to determine if it speaks any version of QUIC.
bool FLAGS_version_mismatch_ok = false;
// If true, an HTTP response code of 3xx is considered to be a successful
// response, otherwise a failure.
bool FLAGS_redirect_is_success = true;
// Enable mTCP-like N-stream simulation for congestion control.
bool FLAGS_mtcp_enabled = false;
// Enable FEC.
bool FLAGS_fec_enabled = false;
// Disable packet pacing.
bool FLAGS_pacing_enabled = true;
// Send N parallel requests.
int32 FLAGS_requests = 1;
// Congestion control with N emulated connections.
int32 FLAGS_emulated_connections = 0;
// Set ICWND to 3.
bool FLAGS_icwnd03 = false;
// Set ICWND to 10.
bool FLAGS_icwnd10 = false;
// Set ICWND to 10.
bool FLAGS_icwnd50 = false;

int main(int argc, char *argv[]) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* line = base::CommandLine::ForCurrentProcess();
  const base::CommandLine::StringVector& urls = line->GetArgs();

  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  CHECK(logging::InitLogging(settings));

  if (line->HasSwitch("h") || line->HasSwitch("help") || urls.empty()) {
    const char* help_str =
        "Usage: quic_client [options] <url>\n"
        "\n"
        "<url> with scheme must be provided (e.g. http://www.google.com)\n\n"
        "Options:\n"
        "-h, --help                  show this help message and exit\n"
        "--host=<host>               specify the IP address of the hostname to "
        "connect to\n"
        "--port=<port>               specify the port to connect to\n"
        "--body=<body>               specify the body to post\n"
        "--headers=<headers>         specify a semicolon separated list of "
        "key:value pairs to add to request headers\n"
        "--mtcp                      enable mTCP like behavior for congestion control\n"
        "--fec                       enable FEC\n"
        "--emulated-connections=<N>  congestion control with N emulated connections (4,8,16,32,64)\n"
        "--requests=<requests>       send multiple requests of the specified url\n"
        "--disable-pacing            disable packet pacing\n"
        "--icwnd03                   set ICWND to 3\n"
        "--icwnd10                   set ICWND to 10\n"
        "--icwnd50                   set ICWND to 50\n"
        "--quiet                     specify for a quieter output experience\n"
        "--quic-version=<quic version> specify QUIC version to speak\n"
        "--version_mismatch_ok       if specified a version mismatch in the "
        "handshake is not considered a failure\n"
        "--redirect_is_success       if specified an HTTP response code of 3xx "
        "is considered to be a successful response, otherwise a failure\n";
    cout << help_str;
    exit(0);
  }
  if (line->HasSwitch("host")) {
    FLAGS_host = line->GetSwitchValueASCII("host");
  }
  if (line->HasSwitch("port")) {
    if (!base::StringToInt(line->GetSwitchValueASCII("port"), &FLAGS_port)) {
      std::cerr << "--port must be an integer\n";
      return 1;
    }
  }
  if (line->HasSwitch("requests")) {
    if (!base::StringToInt(line->GetSwitchValueASCII("requests"), &FLAGS_requests)) {
      std::cerr << "--requests must be an integer\n";
      return 1;
    }
  }
  if (line->HasSwitch("emulated-connections")) {
    if (!base::StringToInt(line->GetSwitchValueASCII("emulated-connections"),
             &FLAGS_emulated_connections)) {
      std::cerr << "--emulated-connections must be an integer\n";
      return 1;
    }
    if (FLAGS_emulated_connections < 4 || FLAGS_emulated_connections > 64 ||
        (FLAGS_emulated_connections & (FLAGS_emulated_connections - 1)) != 0) {
      std::cerr << "--emulated-connections can be one of the following values: 4,8,16,32,64.";
      return 1;
    }
  }
  if (line->HasSwitch("body")) {
    FLAGS_body = line->GetSwitchValueASCII("body");
  }
  if (line->HasSwitch("headers")) {
    FLAGS_headers = line->GetSwitchValueASCII("headers");
  }
  if (line->HasSwitch("quiet")) {
    FLAGS_quiet = true;
  }
  if (line->HasSwitch("quic-version")) {
    int quic_version;
    if (base::StringToInt(line->GetSwitchValueASCII("quic-version"),
                          &quic_version)) {
      FLAGS_quic_version = quic_version;
    }
  }
  if (line->HasSwitch("version_mismatch_ok")) {
    FLAGS_version_mismatch_ok = true;
  }
  if (line->HasSwitch("redirect_is_success")) {
    FLAGS_redirect_is_success = true;
  }
  if (line->HasSwitch("mtcp")) {
    if (FLAGS_emulated_connections == 0) {
      FLAGS_mtcp_enabled = true;
    } else {
      std::cerr << "--mtcp has been suppressed because --emulated-connections is used";
    }
  }
  if (line->HasSwitch("fec")) {
    FLAGS_fec_enabled = true;
  }
  if (line->HasSwitch("disable-pacing")) {
    FLAGS_pacing_enabled = false;
  }
  if (line->HasSwitch("icwnd03")) {
    FLAGS_icwnd03 = true;
  }
  if (line->HasSwitch("icwnd10")) {
    FLAGS_icwnd10 = true;
  }
  if (line->HasSwitch("icwnd50")) {
    FLAGS_icwnd50 = true;
  }

  VLOG(1) << "server host: " << FLAGS_host << " port: " << FLAGS_port
          << " body: " << FLAGS_body << " headers: " << FLAGS_headers
          << " quiet: " << FLAGS_quiet
          << " quic-version: " << FLAGS_quic_version
          << " version_mismatch_ok: " << FLAGS_version_mismatch_ok
          << " redirect_is_success: " << FLAGS_redirect_is_success;

  base::AtExitManager exit_manager;

  // Determine IP address to connect to from supplied hostname.
  // TODO(dimm): Shortcut to use a local address.
  net::IPAddressNumber ip_addr = {127,0,0,1};
#if 0
  // TODO(rtenneti): GURL's doesn't support default_protocol argument, thus
  // protocol is required in the URL.
  GURL url(urls[0]);
  string host = FLAGS_host;
  if (host.empty()) {
    host = url.host();
  }
  if (!net::ParseIPLiteralToNumber(host, &ip_addr)) {
    net::AddressList addresses;
    int rv = net::tools::SynchronousHostResolver::Resolve(host, &addresses);
    if (rv != net::OK) {
      LOG(ERROR) << "Unable to resolve '" << host << "' : "
                 << net::ErrorToShortString(rv);
      return 1;
    }
    ip_addr = addresses[0].address();
  }
#endif
  string host_port = net::IPAddressToStringWithPort(ip_addr, FLAGS_port);
//  VLOG(1) << "Resolved " << host << " to " << host_port << endl;
  VLOG(1) << "Resolved " << FLAGS_host << " to " << host_port << endl;

  // Build the client, and try to connect.
  bool is_https = (FLAGS_port == 443);
  net::EpollServer epoll_server;
  net::QuicServerId server_id(FLAGS_host/*url.host()*/, FLAGS_port, is_https,
                              net::PRIVACY_MODE_DISABLED);
  net::QuicVersionVector versions = net::QuicSupportedVersions();
  if (FLAGS_quic_version != -1) {
    versions.clear();
    versions.push_back(static_cast<net::QuicVersion>(FLAGS_quic_version));
  }

  net::QuicConfig config;
  net::QuicTagVector copt;
  if (FLAGS_mtcp_enabled) {
    copt.push_back(net::kNCON);
  }
#if 0  
  if (FLAGS_fec_enabled) {
    copt.push_back(net::kFHDR);
    copt.push_back(net::kFSTR);
  }
  if (FLAGS_pacing_enabled) {
    copt.push_back(net::kDPAC);
  }
  if (FLAGS_emulated_connections == 4) {
    copt.push_back(net::k4CON);
  }
  if (FLAGS_emulated_connections == 8) {
    copt.push_back(net::k8CON);
  }
  if (FLAGS_emulated_connections == 16) {
    copt.push_back(net::kDCON);
  }
  if (FLAGS_emulated_connections == 32) {
    copt.push_back(net::kQCON);
  }
  if (FLAGS_emulated_connections == 64) {
    copt.push_back(net::kOCON);
  }
#endif
  if (FLAGS_icwnd03) {
    copt.push_back(net::kIW03);
  }
  if (FLAGS_icwnd10) {
    copt.push_back(net::kIW10);
  }
  if (FLAGS_icwnd50) {
    copt.push_back(net::kIW50);
  }
  config.SetConnectionOptionsToSend(copt);

  net::tools::QuicClient client(net::IPEndPoint(ip_addr, FLAGS_port), server_id,
                                versions, config, &epoll_server);
  if (!client.Initialize()) {
    cerr << "Failed to initialize client." << endl;
    return 1;
  }
  if (!client.Connect()) {
    net::QuicErrorCode error = client.session()->error();
    if (FLAGS_version_mismatch_ok && error == net::QUIC_INVALID_VERSION) {
      cout << "Server talks QUIC, but none of the versions supported by "
           << "this client: " << QuicVersionVectorToString(versions) << endl;
      // Version mismatch is not deemed a failure.
      return 0;
    }
    cerr << "Failed to connect to " << host_port
         << ". Error: " << net::QuicUtils::ErrorToString(error) << endl;
    return 1;
  }
  cout << "Connected to " << host_port << endl;

  // Make sure to store the response, for later output.
  client.set_store_response(true);
  if (FLAGS_quiet) {
    client.set_store_response_body_and_headers(false);
  }

  client.SendRequestsAndWaitForResponse(urls);

  // Print request and response details.
  if (!FLAGS_quiet) {
    cout << "Response:" << endl;
    cout << "body: " << client.latest_response_body() << endl;
  }
}
