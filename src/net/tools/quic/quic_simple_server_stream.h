// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_QUIC_SPDY_SERVER_STREAM_H_
#define NET_TOOLS_QUIC_QUIC_SPDY_SERVER_STREAM_H_

#include <string>

#include "base/basictypes.h"
#include "net/quic/reliable_quic_stream.h"
#include "net/quic/quic_protocol.h"

namespace net {

namespace tools {

class QuicServerSession;
static const QuicPriority kDefaultPriority = 3;

class QuicSimpleServerStream : public ReliableQuicStream {
 public:
  QuicSimpleServerStream(QuicStreamId id, QuicServerSession* session);
  ~QuicSimpleServerStream() override;

  // ReliableQuicStream implementation called by the session when there's
  // data for us.
  uint32 ProcessRawData(const char* data, uint32 data_len) override;
  void OnFinRead() override;
  QuicPriority EffectivePriority() const override { return kDefaultPriority; }

 protected:
  virtual void SendResponse();

 private:
  int content_length_;
  std::string body_;

  DISALLOW_COPY_AND_ASSIGN(QuicSimpleServerStream);
};

}  // namespace tools
}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_SPDY_SERVER_STREAM_H_
