// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/quic_simple_server_stream.h"

#include "net/tools/quic/quic_server_session.h"

#include "base/logging.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"

using base::StringPiece;
using base::StringToInt;
using std::string;

namespace net {
namespace tools {

QuicSimpleServerStream::QuicSimpleServerStream(QuicStreamId id,
                                           QuicServerSession* session)
    : ReliableQuicStream(id, session), content_length_(-1) {
}

QuicSimpleServerStream::~QuicSimpleServerStream() {
}

uint32 QuicSimpleServerStream::ProcessRawData(const char* data, uint32 data_len) {
  body_.append(data, data_len);
  DVLOG(1) << "################ Processed " << data_len << " bytes for stream " << id();
  DVLOG(1) << body_;
  return data_len;
}

void QuicSimpleServerStream::OnFinRead() {
  DVLOG(1) << "################ FIN received for stream ################" << id();
  ReliableQuicStream::OnFinRead();
  if (write_side_closed() || fin_buffered()) {
    return;
  }

  SendResponse();
}

void QuicSimpleServerStream::SendResponse() {
  DVLOG(1) << "Sending response for stream " << id();
  WriteOrBufferData("Hello from the simple QUIC server!", true /*FIN*/, nullptr);
}

}  // namespace tools
}  // namespace net
