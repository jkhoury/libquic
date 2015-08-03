// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/quic_simple_client_stream.h"

#include "base/logging.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "net/tools/quic/quic_client_session.h"

using base::StringPiece;
using std::string;
using base::StringToInt;

namespace net {
namespace tools {

QuicSimpleClientStream::QuicSimpleClientStream(QuicStreamId id,
                                           QuicClientSession* session)
    : QuicDataStream(id, session) {
//    : ReliableQuicStream(id, session) {
}

QuicSimpleClientStream::~QuicSimpleClientStream() {
}

void QuicSimpleClientStream::OnStreamFrame(const QuicStreamFrame& frame) {
  if (!write_side_closed()) {
    DVLOG(1) << "Got a response before the request was complete.  "
             << "Aborting request.";
    CloseWriteSide();
  }
  QuicDataStream::OnStreamFrame(frame);
}

uint32 QuicSimpleClientStream::ProcessData(const char* data, uint32 data_len) {
  data_.append(data, data_len);
  DVLOG(1) << "************ Client processed " << data_len << " bytes for stream " << id();
  return data_len;
}

bool QuicSimpleClientStream::SendRequest(const std::string& request, bool fin) {
  WriteOrBufferData(request, fin, nullptr);
  return true;
}

}  // namespace tools
}  // namespace net
