// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/quic_client.h"

#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "base/logging.h"
#include "net/base/net_util.h"
#include "net/quic/crypto/quic_random.h"
#include "net/quic/quic_connection.h"
#include "net/quic/quic_protocol.h"
#include "net/quic/quic_server_id.h"
#include "net/tools/epoll_server/epoll_server.h"
#include "net/tools/quic/quic_epoll_connection_helper.h"
#include "net/tools/quic/quic_socket_utils.h"
#include "net/tools/quic/file_downloader_client_stream.h"

#include "net/tools/quic/net_util.h"

#ifndef SO_RXQ_OVFL
#define SO_RXQ_OVFL 40
#endif

using std::string;
using std::vector;

namespace net {
namespace tools {

const int kEpollFlags = EPOLLIN | EPOLLOUT | EPOLLET;

QuicClient::QuicClient(IPEndPoint server_address,
                       const QuicServerId& server_id,
                       const QuicVersionVector& supported_versions,
                       EpollServer* epoll_server)
    : server_address_(server_address),
      server_id_(server_id),
      local_port_(0),
      epoll_server_(epoll_server),
      fd_(-1),
      helper_(CreateQuicConnectionHelper()),
      initialized_(false),
      packets_dropped_(0),
      overflow_supported_(false),
      supported_versions_(supported_versions),
      store_response_(false) {
}

QuicClient::QuicClient(IPEndPoint server_address,
                       const QuicServerId& server_id,
                       const QuicVersionVector& supported_versions,
                       const QuicConfig& config,
                       EpollServer* epoll_server)
    : server_address_(server_address),
      server_id_(server_id),
      config_(config),
      local_port_(0),
      epoll_server_(epoll_server),
      fd_(-1),
      helper_(CreateQuicConnectionHelper()),
      initialized_(false),
      packets_dropped_(0),
      overflow_supported_(false),
      supported_versions_(supported_versions),
      store_response_(false) {
}

QuicClient::~QuicClient() {
  if (connected()) {
    session()->connection()->SendConnectionClose(QUIC_PEER_GOING_AWAY);
  }

  CleanUpUDPSocketImpl();
}

bool QuicClient::Initialize() {
  DCHECK(!initialized_);

  // If an initial flow control window has not explicitly been set, then use the
  // same values that Chrome uses.
  const uint32 kSessionMaxRecvWindowSize = 32 * 1024 * 1024;  // 32 MB
  const uint32 kStreamMaxRecvWindowSize = 6 * 1024 * 1024;    //  6 MB
  if (config_.GetInitialStreamFlowControlWindowToSend() ==
      kMinimumFlowControlSendWindow) {
    config_.SetInitialStreamFlowControlWindowToSend(kStreamMaxRecvWindowSize);
  }
  if (config_.GetInitialSessionFlowControlWindowToSend() ==
      kMinimumFlowControlSendWindow) {
    config_.SetInitialSessionFlowControlWindowToSend(kSessionMaxRecvWindowSize);
  }

  epoll_server_->set_timeout_in_us(50 * 1000);

  if (!CreateUDPSocket()) {
    return false;
  }

  epoll_server_->RegisterFD(fd_, this, kEpollFlags);
  initialized_ = true;
  return true;
}

QuicClient::DummyPacketWriterFactory::DummyPacketWriterFactory(
    QuicPacketWriter* writer)
    : writer_(writer) {}

QuicClient::DummyPacketWriterFactory::~DummyPacketWriterFactory() {}

QuicPacketWriter* QuicClient::DummyPacketWriterFactory::Create(
    QuicConnection* /*connection*/) const {
  return writer_;
}


bool QuicClient::CreateUDPSocket() {
  int address_family = server_address_.GetSockAddrFamily();
  fd_ = socket(address_family, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
  if (fd_ < 0) {
    LOG(ERROR) << "CreateSocket() failed: " << strerror(errno);
    return false;
  }

  int get_overflow = 1;
  int rc = setsockopt(fd_, SOL_SOCKET, SO_RXQ_OVFL, &get_overflow,
                      sizeof(get_overflow));
  if (rc < 0) {
    DLOG(WARNING) << "Socket overflow detection not supported";
  } else {
    overflow_supported_ = true;
  }

  const uint32 kSocketBufferSize = 32 * 1024 * 1024; // 32 MB

  if (!QuicSocketUtils::SetReceiveBufferSize(fd_,
                                             kSocketBufferSize)) {
    return false;
  }

  if (!QuicSocketUtils::SetSendBufferSize(fd_, kSocketBufferSize)) {
    return false;
  }

  rc = QuicSocketUtils::SetGetAddressInfo(fd_, address_family);
  if (rc < 0) {
    LOG(ERROR) << "IP detection not supported" << strerror(errno);
    return false;
  }
#if 0
  if (bind_to_address_.size() != 0) {
    client_address_ = IPEndPoint(bind_to_address_, local_port_);
  } else if (address_family == AF_INET) {
    // TODO(dimm): fix address translations.
    IPAddressNumber any4(16, 0);
//    CHECK(net::ParseIPLiteralToNumber("0.0.0.0", &any4));
    client_address_ = IPEndPoint(any4, local_port_);
  } else {
    IPAddressNumber any6(16, 0);
//    CHECK(net::ParseIPLiteralToNumber("::", &any6));
    client_address_ = IPEndPoint(any6, local_port_);
  }

  sockaddr_storage raw_addr;
  socklen_t raw_addr_len = sizeof(raw_addr);
  CHECK(client_address_.ToSockAddr(reinterpret_cast<sockaddr*>(&raw_addr),
                           &raw_addr_len));
  rc = bind(fd_,
            reinterpret_cast<const sockaddr*>(&raw_addr),
            sizeof(raw_addr));
  if (rc < 0) {
    LOG(ERROR) << "Bind failed: " << strerror(errno);
    return false;
  }
#endif
  SockaddrStorage storage;
  if (getsockname(fd_, storage.addr, &storage.addr_len) != 0 ||
      !client_address_.FromSockAddr(storage.addr, storage.addr_len)) {
    LOG(ERROR) << "Unable to get self address.  Error: " << strerror(errno);
  }

  return true;
}

bool QuicClient::Connect() {
  StartConnect();
  while (EncryptionBeingEstablished()) {
    WaitForEvents();
  }
  return session_->connection()->connected();
}

QuicClientSession* QuicClient::CreateQuicClientSession(
    const QuicConfig& config,
    QuicConnection* connection,
    const QuicServerId& server_id,
    QuicCryptoClientConfig* crypto_config) {
  return new QuicClientSession(config, connection, server_id_, &crypto_config_);
}

void QuicClient::StartConnect() {
  DCHECK(initialized_);
  DCHECK(!connected());

  QuicPacketWriter* writer = CreateQuicPacketWriter();

  DummyPacketWriterFactory factory(writer);

  session_.reset(CreateQuicClientSession(
      config_,
      new QuicConnection(GenerateConnectionId(), server_address_, helper_.get(),
                         factory,
                         /* owns_writer= */ false, Perspective::IS_CLIENT,
                         server_id_.is_https(), supported_versions_),
      server_id_, &crypto_config_));

  // Reset |writer_| after |session_| so that the old writer outlives the old
  // session.
  if (writer_.get() != writer) {
    writer_.reset(writer);
  }
  session_->Initialize();
  session_->CryptoConnect();
}

bool QuicClient::EncryptionBeingEstablished() {
  return !session_->IsEncryptionEstablished() &&
      session_->connection()->connected();
}

void QuicClient::Disconnect() {
  DCHECK(initialized_);

  if (connected()) {
    session()->connection()->SendConnectionClose(QUIC_PEER_GOING_AWAY);
  }

  CleanUpUDPSocket();

  initialized_ = false;
}

void QuicClient::CleanUpUDPSocket() {
  CleanUpUDPSocketImpl();
}

void QuicClient::CleanUpUDPSocketImpl() {
  if (fd_ > -1) {
    epoll_server_->UnregisterFD(fd_);
    int rc = close(fd_);
    DCHECK_EQ(0, rc);
    fd_ = -1;
  }
}

bool QuicClient::SendRequest(const string& request,
                             bool fin) {
  if (!connected()) {
    return false;
  }

  FileDownloaderClientStream* stream = session_->CreateOutgoingDynamicStream();
  if (stream == nullptr) {
    LOG(DFATAL) << "stream creation failed!";
    return false;
  }
  stream->set_visitor(this);
  return stream->SendRequest(request, fin);
}

void QuicClient::SendRequestsAndWaitForResponse(
    const vector<string>& url_list) {
  bool any_request_succedded = false;
  for (size_t i = 0; i < url_list.size(); ++i) {
    any_request_succedded |= SendRequest(url_list[i], true);
  }
  if (any_request_succedded)
    while (WaitForEvents()) {}
}

void QuicClient::WaitForStreamToClose(QuicStreamId id) {
  DCHECK(connected());

  while (connected() && !session_->IsClosedStream(id)) {
    WaitForEvents();
  }
}

void QuicClient::WaitForCryptoHandshakeConfirmed() {
  DCHECK(connected());

  while (connected() && !session_->IsCryptoHandshakeConfirmed()) {
    WaitForEvents();
  }
}

bool QuicClient::WaitForEvents() {
  DCHECK(connected());

  epoll_server_->WaitForEventsAndExecuteCallbacks();
  return session_->num_active_requests() != 0;
}

void QuicClient::OnEvent(int fd, EpollEvent* event) {
  DCHECK_EQ(fd, fd_);

  if (event->in_events & EPOLLIN) {
    while (connected() && ReadAndProcessPacket()) {
    }
  }
  if (connected() && (event->in_events & EPOLLOUT)) {
    writer_->SetWritable();
    session_->connection()->OnCanWrite();
  }
  if (event->in_events & EPOLLERR) {
    DVLOG(1) << "Epollerr";
  }
}

void QuicClient::OnClose(FileDownloaderClientStream* stream) {
  DVLOG(1) << "OnClose() for StreamID " << stream->id();
}

bool QuicClient::connected() const {
  return session_.get() && session_->connection() &&
      session_->connection()->connected();
}

bool QuicClient::goaway_received() const {
  return session_ != nullptr && session_->goaway_received();
}

QuicConnectionId QuicClient::GenerateConnectionId() {
  return QuicRandom::GetInstance()->RandUint64();
}

QuicEpollConnectionHelper* QuicClient::CreateQuicConnectionHelper() {
  return new QuicEpollConnectionHelper(epoll_server_);
}

QuicPacketWriter* QuicClient::CreateQuicPacketWriter() {
  return new QuicDefaultPacketWriter(fd_);
}

int QuicClient::ReadPacket(char* buffer,
                           int buffer_len,
                           IPEndPoint* server_address,
                           IPAddressNumber* client_ip) {
  return QuicSocketUtils::ReadPacket(
      fd_, buffer, buffer_len,
      overflow_supported_ ? &packets_dropped_ : nullptr, client_ip,
      server_address);
}

bool QuicClient::ReadAndProcessPacket() {
  // Allocate some extra space so we can send an error if the server goes over
  // the limit.
  char buf[2 * kMaxPacketSize];

  IPEndPoint server_address;
  IPAddressNumber client_ip;

  int bytes_read = ReadPacket(buf, arraysize(buf), &server_address, &client_ip);

  if (bytes_read < 0) {
    return false;
  }

  QuicEncryptedPacket packet(buf, bytes_read, false);

  IPEndPoint client_address(client_ip, client_address_.port());
  session_->connection()->ProcessUdpPacket(
      client_address, server_address, packet);
  return true;
}

}  // namespace tools
}  // namespace net
