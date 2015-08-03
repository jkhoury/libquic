// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_QUIC_SPDY_CLIENT_STREAM_H_
#define NET_TOOLS_QUIC_QUIC_SPDY_CLIENT_STREAM_H_

#include <sys/types.h>
#include <string>

#include "base/basictypes.h"
#include "base/strings/string_piece.h"
//#include "net/quic/reliable_quic_stream.h"
#include "net/quic/quic_data_stream.h"
#include "net/quic/quic_protocol.h"

namespace net {
namespace tools {

static const QuicPriority kDefaultPriority = 3;
class QuicClientSession;

class QuicSimpleClientStream : public QuicDataStream /*ReliableQuicStream*/ {
 public:
  QuicSimpleClientStream(QuicStreamId id, QuicClientSession* session);
  ~QuicSimpleClientStream() override;

  // Override the base class to close the write side as soon as we get a
  // response.
  void OnStreamFrame(const QuicStreamFrame& frame) override;

  // ReliableQuicStream implementation called by the session when there's
  // data for us.
  uint32 ProcessData(const char* data, uint32 data_len) override;

  bool SendRequest(const std::string& request, bool fin);

  QuicPriority EffectivePriority() const override { return kDefaultPriority; }

  // Returns the response data.
  const std::string& data() { return data_; }

 private:
  std::string data_;

  DISALLOW_COPY_AND_ASSIGN(QuicSimpleClientStream);
};

}  // namespace tools
}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_SPDY_CLIENT_STREAM_H_
