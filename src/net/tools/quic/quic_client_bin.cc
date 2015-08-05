// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A binary wrapper for QuicClient.
// Connects to a host using QUIC, sends a request to the provided URL, and
// displays the response.

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
int32 FLAGS_port = 6121;
// Set to true for a quieter output experience.
bool FLAGS_quiet = false;
// QUIC version to speak, e.g. 21. If not set, then all available versions are
// offered in the handshake.
int32 FLAGS_quic_version = -1;
// If true, a version mismatch in the handshake is not considered a failure.
// Useful for probing a server to determine if it speaks any version of QUIC.
bool FLAGS_version_mismatch_ok = false;
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
        "Usage: quic_client [options] <file-to-download>\n"
        "\n"
        "Options:\n"
        "-h, --help                  show this help message and exit\n"
        "--host=<host>               specify the IP address of the hostname to "
        "connect to\n"
        "--port=<port>               specify the port to connect to\n"
        "--mtcp                      enable mTCP like behavior for congestion control\n"
        "--fec                       enable FEC\n"
        "--emulated-connections=<N>  congestion control with N emulated connections (4,8,16,32,64)\n"
        "--requests=<requests>       send multiple requests of the specified file\n"
        "--disable-pacing            disable packet pacing\n"
        "--icwnd03                   set ICWND to 3\n"
        "--icwnd10                   set ICWND to 10\n"
        "--icwnd50                   set ICWND to 50\n"
        "--quiet                     specify for a quieter output experience\n"
        "--quic-version=<quic version> specify QUIC version to speak\n"
        "--version_mismatch_ok       if specified a version mismatch in the "
        "handshake is not considered a failure\n";
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
          << " quiet: " << FLAGS_quiet
          << " quic-version: " << FLAGS_quic_version
          << " version_mismatch_ok: " << FLAGS_version_mismatch_ok;

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
  net::QuicServerId server_id(FLAGS_host, FLAGS_port, is_https,
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
//  cout << "Connected to " << host_port << endl;

  // Make sure to store the response, for later output.
  client.set_store_response(true);
  client.SendRequestsAndWaitForResponse(urls);
}
